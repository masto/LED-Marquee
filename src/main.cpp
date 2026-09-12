// Copyright 2022 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Suppress annoying warnings
#define FASTLED_INTERNAL

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <AsyncMqttClient.h>
#include <FS.h>
#include <FastLED.h>
#include <FontMatrise.h>
#include <LEDMatrix.h>
#include <LEDText.h>
#include <SPIFFS.h>
#include <WiFiManager.h>
#include <interpolate.h>
// Needed to resolve conflict between ArduinoOTA and ESPAsyncWebServer
#define WEBSERVER_H
#include <ESPAsyncWebServer.h>

#include <memory>
#include <string>

extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
}

#include "clock.h"
#include "debug_serial.h"
#include "display_manager.h"
#include "marquee_config.h"
#include "system_health.h"
#include "text_layout.h"
#include "text_scroller.h"
#include "text_with_clock_layout.h"
#include "user_config.h"

// How long the loop can stall before the task watchdog resets the chip.
// FastLED.show() blocks while clocking out pixels (which is fairly quick)
// and AsyncMqttClient/AsyncWebServer callbacks can briefly hold the state
// mutex, so 30s is comfortably above any expected normal stall.
constexpr uint32_t kWatchdogTimeoutSec = 30;

// How long to wait in WiFiManager's auto-opened config portal before giving
// up and rebooting. WiFiManager opens its AP if the saved credentials don't
// connect on boot, which can happen for a transient reason (router rebooting
// while we are too); we don't want a momentary outage to leave the marquee
// stuck waiting for someone to walk over and configure it. Long enough for
// an unhurried first-time setup, and matches setConfigPortalTimeout(300).
constexpr unsigned long kAutoConfigRebootMs = 5UL * 60UL * 1000UL;

// How long the reset pin must read LOW without interruption before we enter
// the runtime web portal. The pin has only the internal pull-up, so a couple
// of point samples 50ms apart can be fooled by a glitch; a full second of
// unbroken LOW can't.
constexpr unsigned long kResetHoldMs = 1000;

// How long to stay in the button-triggered web portal before rebooting. A real
// configuration session ends in SaveConfigAndRestart() well before this; it
// bounds the damage if the pin is ever read LOW spuriously, since nothing else
// ever leaves that state.
constexpr unsigned long kButtonConfigRebootMs = 10UL * 60UL * 1000UL;

// How often to publish the retained diagnostic snapshot while MQTT is up.
// Building and publishing it takes a few milliseconds in the main loop, which
// at high scroll rates could show as a hitch; set to 0 to disable the
// periodic publish (connect/transition/restart/on-demand snapshots remain).
constexpr unsigned long kDiagIntervalSec = 0;

// Shared-state mutex.
//
// The marquee has three threads of execution that all touch display state and
// shared variables: the Arduino main loop, the AsyncTCP task (which fires
// AsyncWebServer and AsyncMqttClient callbacks), and the FreeRTOS timer task
// (which fires the MQTT reconnect timer). Without serialization, two threads
// can simultaneously mutate `String scroll_next` (heap allocation), reconfigure
// the cLEDText state machine, or call FastLED.show() while another caller is
// repainting the buffer. That's a real source of intermittent crashes.
//
// Any code that touches shared state (animation, scroll_next, layout->text()
// mutating calls, FastLED.show()) takes this mutex first. Critical sections
// are short. The mutex is recursive so helpers that need the lock (e.g.
// PublishDiag) can be called from code that already holds it.
SemaphoreHandle_t g_state_mutex = nullptr;

// RAII helper for the state mutex.
class StateLock {
 public:
  StateLock() {
    if (g_state_mutex) xSemaphoreTakeRecursive(g_state_mutex, portMAX_DELAY);
  }
  ~StateLock() {
    if (g_state_mutex) xSemaphoreGiveRecursive(g_state_mutex);
  }
  StateLock(const StateLock&) = delete;
  StateLock& operator=(const StateLock&) = delete;
};

// Pending crash report, published once MQTT comes up.
String pending_crash_report;

typedef const char* FsLabel;
const FsLabel kUserFsLabel = "/user";
const FsLabel kSpiffsFsLabel = "/spiffs";
const String kConfigFileName = "/config.json";

std::shared_ptr<WiFiManager> wm = std::make_shared<WiFiManager>();
AsyncWebServer server(80);
AsyncMqttClient mqtt_client;
CEveryNMillis* scroll_timer;
TimerHandle_t mqtt_reconnect_timer;
std::unique_ptr<fs::SPIFFSFS> web_fs;
std::shared_ptr<led_marquee::DisplayManager> display_manager;
std::unique_ptr<led_marquee::TextWithClockLayout> layout;

uint8_t clock_hue = 0;
unsigned int scroll_speed = 40;
bool is_connected = false;
bool enable_display = true;
bool enable_clock = kClockWidth > 0;
bool enable_ota = false;
bool config_mode = false;
bool should_save_config = false;
String scroll_next;

// Set when WiFiManager opens its config portal on its own (i.e., the saved
// credentials failed to connect at boot). Used to detect "stuck in auto
// config portal" and reboot, distinguishing it from the runtime button-press
// path (which calls startWebPortal() without going through ConfigModeCallback).
unsigned long auto_config_entered_ms = 0;

// Set when the runtime (reset pin) web portal is entered, so it can be timed
// out the same way.
unsigned long button_config_entered_ms = 0;

// Diagnostic state, published in the <node>/diag snapshot. See PublishDiag().
bool auto_config_seen = false;  // this boot went through the auto portal
String config_prompt;           // exact text ConfigModeCallback put on screen
bool autoconnect_ok = false;
unsigned long autoconnect_ms = 0;
uint32_t mqtt_connect_count = 0;
volatile uint32_t wifi_disconnect_events = 0;
volatile uint8_t wifi_last_disconnect_reason = 0;
volatile uint32_t wifi_last_disconnect_s = 0;
unsigned long prompt_stuck_since_ms = 0;

String mqtt_node_topic;
String mqtt_command_topic;
String mqtt_ready_topic;
String mqtt_diag_topic;

led_marquee::UserConfig config(wm);

// Process one tick of the animation loop
void AnimateScroller() {
  static bool scroll_wait = false;
  static unsigned long wait_start;

  if (!scroll_wait) {
    if (!layout->text().Animate()) {
      // Hit the end. First, make sure we're in normal mode.
      if (config_mode) {
        // In config mode, we're not processing the queue.
        layout->text().ShowScrollText();
      }
      // Is there a new message queued?
      else if (!scroll_next.isEmpty()) {
        // Something's queued up. Show it.
        layout->text().ShowScrollText(scroll_next);
        // Clear the queue
        scroll_next.clear();
        // Allow clients to queue ahead and avoid the time delay.
        mqtt_client.publish(mqtt_ready_topic.c_str(), 0, false,
                            "{\"ready\": false}");
      } else {
        // Nothing queued. Notify and wait for a new message to come in.
        scroll_wait = true;
        mqtt_client.publish(mqtt_ready_topic.c_str(), 0, false,
                            "{\"ready\": true}");
        wait_start = millis();
      }
    }
  } else {
    // We're in the waiting period. See if it's up.
    unsigned long m = millis();
    if (m - wait_start > kSmWaitTime) {
      // Yes, it is. Resuming scrolling, with a new message if we have one.
      scroll_wait = false;
      if (!scroll_next.isEmpty()) {
        // Something's queued up
        layout->text().ShowScrollText(scroll_next);
        // Clear the queue
        scroll_next.clear();
      } else {
        // Restart the existing message.
        layout->text().ShowScrollText();
      }
      // Allow clients to queue ahead and avoid the time delay.
      mqtt_client.publish(mqtt_ready_topic.c_str(), 0, false,
                          "{\"ready\": false}");
    }
  }
}

// Resets the layout to use the full display for text. This is intended for
// things like entering configuration mode: there is no returning to normal
// without rebooting.
void RemoveClock() {
  enable_clock = false;
  layout = std::make_unique<led_marquee::TextWithClockLayout>(
      *display_manager, kTextFont, 0, kClockFont);
}

// Publish a snapshot of everything relevant to "what state is the marquee
// in?" to <node>/diag, retained, so it can be inspected after the fact even if
// nobody had an MQTT client open at the time. Published on MQTT connect, every
// kDiagIntervalSec, on state transitions, right before a planned restart, and
// on demand (any message to <node>/diag/get). No-op until MQTT is up.
//
// The rolling <node>/diag is overwritten by the next boot's first publish, so
// snapshots worth keeping across a reboot (the state right before a planned
// restart, or an anomaly) are also published to <node>/diag/<keep_as>.
//
// Takes the state mutex (it reads display state); safe to call from code that
// already holds it.
void PublishDiag(const char* event, const char* keep_as = nullptr) {
  if (mqtt_diag_topic.isEmpty() || !mqtt_client.connected()) return;

  StateLock lock;

  DynamicJsonDocument doc(1536);
  doc["event"] = event;
  doc["boot"] = led_marquee::GetBootCount();
  doc["uptime_s"] = millis() / 1000UL;
  doc["reset_reason"] = led_marquee::GetResetReasonName();
  doc["last_restart"] = led_marquee::GetLastRestartInfo();

  // Our own state machine.
  doc["is_connected"] = is_connected;
  doc["config_mode"] = config_mode;
  doc["auto_config_seen"] = auto_config_seen;
  doc["auto_config_pending"] = auto_config_entered_ms != 0;
  doc["button_config"] = button_config_entered_ms != 0;
  doc["autoconnect_ok"] = autoconnect_ok;
  doc["autoconnect_ms"] = autoconnect_ms;

  // WiFiManager / WiFi.
  doc["portal_active"] = wm->getConfigPortalActive();
  doc["web_portal_active"] = wm->getWebPortalActive();
  doc["wifi_status"] = static_cast<int>(WiFi.status());
  doc["wifi_mode"] = static_cast<int>(WiFi.getMode());
  doc["ssid"] = WiFi.SSID();
  doc["ip"] = WiFi.localIP().toString();
  doc["rssi"] = WiFi.RSSI();
  doc["wifi_disconnects"] = wifi_disconnect_events;
  doc["wifi_last_disconnect_reason"] = wifi_last_disconnect_reason;
  doc["wifi_last_disconnect_s"] = wifi_last_disconnect_s;
  doc["mqtt_connects"] = mqtt_connect_count;

  // Display. `text` is what the scroller is actually holding, which is the
  // ground truth when the state flags and the screen disagree. The buffer
  // carries LEDText effect codes (raw bytes >= 0x80, plus color parameters),
  // which ArduinoJson would emit verbatim and which aren't valid UTF-8, so
  // keep only printable ASCII.
  doc["display_on"] = enable_display;
  doc["clock"] = enable_clock;
  String text;
  for (char c : layout->text().ScrollBuffer()) {
    if (c >= 0x20 && c < 0x7f) text += c;
    if (text.length() >= 80) break;
  }
  text.trim();
  doc["text"] = text;
  doc["queued_len"] = scroll_next.length();

  // Memory.
  doc["free_heap"] = ESP.getFreeHeap();
  doc["min_free_heap"] = ESP.getMinFreeHeap();
  doc["max_alloc"] = ESP.getMaxAllocHeap();

  String payload;
  serializeJson(doc, payload);
  mqtt_client.publish(mqtt_diag_topic.c_str(), 0, true, payload.c_str());
  if (keep_as != nullptr) {
    String keep_topic = mqtt_diag_topic + "/" + keep_as;
    mqtt_client.publish(keep_topic.c_str(), 0, true, payload.c_str());
  }
  debug_println("diag: " + payload);
}

// The one way to reboot on purpose. Publishes a final retained diag snapshot
// (if MQTT is up) so that, together with the next boot's "last_restart", the
// reason is always recoverable; optionally shows `display_text`; waits
// `delay_ms` for both to land; marks the restart as clean; restarts.
//
// `reason` is a short tag (it's stored in RTC memory across the reboot).
void PlannedRestart(const char* reason, const char* display_text,
                    unsigned long delay_ms = 2000) {
  debug_printf("Planned restart: %s\n", reason);
  led_marquee::SetBreadcrumb(reason);
  {
    StateLock lock;
    if (display_text != nullptr) layout->text().ShowStaticText(display_text);
    String event = String("restart:") + reason;
    PublishDiag(event.c_str(), "restart");
  }
  delay(delay_ms);
  led_marquee::NoteCleanRestart(reason);
  ESP.restart();
}

// Mount a SPIFFS filesystem
std::unique_ptr<fs::SPIFFSFS> GetFileSystem(const FsLabel label) {
  auto fs = std::make_unique<fs::SPIFFSFS>();
  if (!fs->begin(true, label, 10, &label[1])) {
    debug_println("Failed to mount filesystem");
    return nullptr;
  }

  return fs;
}

// Dump the filesystem contents, for debugging
void PrintFileList(fs::SPIFFSFS& fs) {
  File root = fs.open("/");
  File file = root.openNextFile();
  while (file) {
    String fileName = file.name();
    size_t fileSize = file.size();
    debug_printf("FS File: %s, size: %s\n", fileName.c_str(),
                 String(fileSize).c_str());
    file = root.openNextFile();
  }
}

// If `kResetPin` is held low for 3 seconds during startup, erase all settings
// and reboot into setup mode
void CheckForResetConfig() {
  // Cheesy way to do this, but it's sufficient and unimportant
  if (digitalRead(kResetPin) == LOW) {
    delay(50);
    if (digitalRead(kResetPin) == LOW) {
      // "CLEAR?" is too long for one panel :-(
      layout->text().ShowStaticText("CLR?");
      delay(1000);

      if (digitalRead(kResetPin) != LOW) return;
      layout->text().ShowStaticText("CLR?3");
      delay(1000);

      if (digitalRead(kResetPin) != LOW) return;
      layout->text().ShowStaticText("CLR?2");
      delay(1000);

      if (digitalRead(kResetPin) != LOW) return;
      layout->text().ShowStaticText("CLR?1");
      delay(1000);

      if (digitalRead(kResetPin) == LOW) {
        layout->text().ShowStaticText("CLR!");
        debug_println("Clearing settings");
        // Reset WiFiManager config
        wm->resetSettings();
        // Format the user filesystem
        auto fs = GetFileSystem(kUserFsLabel);
        if (fs) {
          fs->end();
          fs->format();
        }
        PlannedRestart("config_cleared", nullptr);
      }
    }
  }
}

// Copy config from the user and flag it to be stored to the filesystem
void SaveParamsCallback() {
  should_save_config = true;

  config.ReadFromWifiManager();
}

// When WiFiManager enters configuration mode, display a prompt. This fires
// only when WiFiManager opens its own AP (i.e., the saved credentials didn't
// connect); the runtime button-press path uses startWebPortal() in STA mode
// and doesn't invoke this callback. We record the time so the main loop can
// reboot us if we get stuck here after a transient WiFi outage.
void ConfigModeCallback(WiFiManager* myWiFiManager) {
  config_mode = true;
  auto_config_seen = true;
  if (auto_config_entered_ms == 0) {
    auto_config_entered_ms = millis();
    // millis() is 0 immediately after boot; bump to 1 so the "is set" check
    // (!= 0) is unambiguous.
    if (auto_config_entered_ms == 0) auto_config_entered_ms = 1;
    led_marquee::SetBreadcrumb("auto_config_portal");
  }
  RemoveClock();

  config_prompt =
      "Connect to " + myWiFiManager->getConfigPortalSSID() + " to configure.";
  layout->text().ShowScrollText(config_prompt);
}

// The exit in the config portal isn't particularly useful, and results in an
// endless loop unless we manually catch it and do something.
void WmWebServerCallback() {
  // Unfortunately, we can't do anything great here, but we can at least reboot
  // to get out of the loop.
  wm->server->on(WM_G(R_exit), [] {
    debug_println("Exiting web config and rebooting");

    wm->server->sendHeader("Cache-Control",
                           "no-cache, no-store, must-revalidate");
    wm->server->send(200, "text/plain", "Bye!");
    PlannedRestart("web_exit", nullptr);
  });
}

// Mount the filesystem, formatting if necessary, and load any JSON config
void LoadUserConfig() {
  auto fs = GetFileSystem(kUserFsLabel);
  if (!fs) return;

  if (fs->exists(kConfigFileName)) {
    debug_println("reading config file");
    File configFile = fs->open(kConfigFileName, "r");
    if (configFile) {
      size_t size = configFile.size();
      // Allocate a buffer to store contents of the file.
      std::unique_ptr<char[]> buf(new char[size]);

      configFile.readBytes(buf.get(), size);

      DynamicJsonDocument json(1024);
      auto deserializeError = deserializeJson(json, buf.get());
      if (!deserializeError) {
        config.ReadFromJson(json);
      } else {
        debug_print("failed to parse json config: ");
        debug_println(deserializeError.c_str());
      }
      json["mqtt_pass"] = "*****";
      serializeJsonPretty(json, Serial);
      debug_println();
      configFile.close();
    }
  }

  fs->end();
}

// Initialize WiFi Manager in non-blocking mode
void SetupWiFiManager() {
  wm->setConfigPortalBlocking(false);

  wm->setAPCallback(ConfigModeCallback);
  wm->setSaveParamsCallback(SaveParamsCallback);
  wm->setWebServerCallback(WmWebServerCallback);

  wm->setConfigPortalTimeout(300);
}

void SetClockColor() { layout->clock().SetColorHsv(clock_hue, 0xff, 0xff); }

void InitLEDs() {
  display_manager =
      led_marquee::DisplayManager::Create<CHIPSET, kLedPins, kColorOrder>(
          kMarqueeSections, kSectionWidth, kPanelHeight,
          std::make_shared<
              cLEDMatrix<(kReverseDirection ? -kMarqueeWidth : kMarqueeWidth),
                         kPanelHeight, kMatrixType>>(),
          true);

  display_manager->SetMaxPower(kLedVolts, 1000.0 * kLedMaxAmps);
  display_manager->SetBrightness(15);

  layout = std::make_unique<led_marquee::TextWithClockLayout>(
      *display_manager, kTextFont, kClockWidth, kClockFont);

  layout->text().SetMaxLength(kMaxMessageLen);
  SetClockColor();
}

// Save config to filesystem. And then reboot to ensure clean initialization.
void SaveConfigAndRestart() {
  should_save_config = false;
  debug_println("saving config");

  DynamicJsonDocument json(1024);
  config.ToJson(json);

  auto fs = GetFileSystem(kUserFsLabel);
  if (fs) {
    File config_file = fs->open(kConfigFileName, "w");
    if (!config_file) {
      debug_println("failed to open config file for writing");
    }

    serializeJson(json, config_file);
    config_file.close();

    fs->end();
  }

  PlannedRestart("config_saved", nullptr);
}

void DumpWmInfo() {
  debug_println();
  debug_print("getConfigPortalActive: ");
  debug_println(wm->getConfigPortalActive());

  debug_print("getConfigPortalSSID: ");
  debug_println(wm->getConfigPortalSSID());

  debug_print("getDefaultAPName: ");
  debug_println(wm->getDefaultAPName());

  debug_print("getLastConxResult: ");
  debug_println(wm->getLastConxResult());

  debug_print("getModeString: ");
  debug_println(wm->getModeString((uint8_t)WiFi.getMode()));

  debug_print("getWebPortalActive: ");
  debug_println(wm->getWebPortalActive());

  debug_print("getWiFiHostname: ");
  debug_println(wm->getWiFiHostname());

  debug_print("getWiFiIsSaved: ");
  debug_println(wm->getWiFiIsSaved());

  debug_print("getWLStatusString: ");
  debug_println(wm->getWLStatusString());
}

void ConnectToMqtt() {
  debug_println("Connecting to MQTT...");
  mqtt_client.connect();
}

// Publish Home Assistant discovery config
void MqttDiscovery() {
  String mqtt_node(config.StringValue("mqtt_node"));
  if (mqtt_node.isEmpty()) return;

  uint8_t wifi_mac[8];
  WiFi.macAddress(wifi_mac);
  String mqtt_client_id = mqtt_node + "-" + String(wifi_mac[0], HEX) +
                          String(wifi_mac[1], HEX) + String(wifi_mac[2], HEX) +
                          String(wifi_mac[3], HEX) + String(wifi_mac[4], HEX) +
                          String(wifi_mac[5], HEX);

  StaticJsonDocument<255> doc;
  doc["name"] = mqtt_node;
  doc["unique_id"] = mqtt_client_id;
  doc["schema"] = "json";
  doc["command_topic"] = mqtt_command_topic;
  doc["brightness"] = true;
  doc["color_mode"] = true;
  doc["supported_color_modes"][0] = "rgb";

  JsonObject device = doc.createNestedObject("device");
  device["identifiers"][0] = mqtt_client_id;
  device["name"] = mqtt_node;

  String payload;
  serializeJson(doc, payload);

  String topic = String(kHaDiscoveryPrefix) + "/light/" + mqtt_node + "/config";
  mqtt_client.publish(topic.c_str(), 0, true, payload.c_str());

  debug_printf("Published HA discovery to %s:\n", topic.c_str());
  serializeJsonPretty(doc, Serial);
  debug_println();
}

void OnMqttConnect(bool sessionPresent) {
  debug_println("Connected to MQTT");
  led_marquee::SetBreadcrumb("mqtt_connect");
  mqtt_connect_count++;

  mqtt_node_topic = String(kMqttPrefix) + "/" + config.StringValue("mqtt_node");
  mqtt_command_topic = mqtt_node_topic + "/set";
  mqtt_ready_topic = mqtt_node_topic + "/ready";
  mqtt_diag_topic = mqtt_node_topic + "/diag";

  String mqtt_subscription = mqtt_node_topic + "/#";
  mqtt_client.subscribe(mqtt_subscription.c_str(), 0);
  debug_println("Subscribed to " + mqtt_subscription);

  MqttDiscovery();

  // Publish a status payload describing this boot, including any crash report
  // from the previous run. Topic: <prefix>/<node>/status, retained so a
  // subscriber that connects later still gets the most recent boot info.
  {
    StaticJsonDocument<512> status;
    status["boot"] = led_marquee::GetBootCount();
    status["uptime_s"] = millis() / 1000UL;
    status["reset_reason"] = led_marquee::GetResetReasonName();
    status["last_restart"] = led_marquee::GetLastRestartInfo();
    status["auto_config"] = auto_config_seen;
    status["autoconnect_ms"] = autoconnect_ms;
    status["free_heap"] = ESP.getFreeHeap();
    status["min_free_heap"] = ESP.getMinFreeHeap();
    status["ip"] = WiFi.localIP().toString();
    status["rssi"] = WiFi.RSSI();
    if (pending_crash_report.length()) {
      status["crash"] = pending_crash_report;
    }
    String status_payload;
    serializeJson(status, status_payload);
    String status_topic = mqtt_node_topic + "/status";
    mqtt_client.publish(status_topic.c_str(), 0, true, status_payload.c_str());

    // Also publish the crash report on its own topic for easier retrieval /
    // alerting. Retained, but cleared on next clean boot.
    String crash_topic = mqtt_node_topic + "/crashlog";
    if (pending_crash_report.length()) {
      mqtt_client.publish(crash_topic.c_str(), 0, true,
                          pending_crash_report.c_str());
    } else {
      // Clear the retained crash log so HA / dashboards don't keep showing
      // stale info after a clean boot.
      mqtt_client.publish(crash_topic.c_str(), 0, true, "");
    }
    pending_crash_report = "";
  }

  PublishDiag("mqtt_connect");
}

void OnMqttMessage(char* topic, char* payload,
                   AsyncMqttClientMessageProperties properties, size_t len,
                   size_t index, size_t total) {
  // AsyncMqttClient may deliver large messages in multiple fragments. We don't
  // attempt to reassemble; for the small JSON commands we expect, a single
  // fragment should always be enough. Skip anything else.
  (void)properties;
  if (index != 0 || len != total) {
    debug_printf("Skipping fragmented MQTT message (len=%u total=%u)\n",
                 (unsigned)len, (unsigned)total);
    return;
  }

  String str_topic = String(topic);

  // Diagnostics on demand; any payload will do.
  if (str_topic == mqtt_diag_topic + "/get") {
    PublishDiag("request");
    return;
  }

  // Our own retained publications come back to us on every (re)subscribe.
  if (str_topic == mqtt_ready_topic || str_topic == mqtt_diag_topic ||
      str_topic.startsWith(mqtt_diag_topic + "/") ||
      str_topic == mqtt_node_topic + "/status" ||
      str_topic == mqtt_node_topic + "/crashlog") {
    return;
  }

  // payload is NOT guaranteed to be null-terminated in AsyncMqttClient; pass
  // the explicit length so ArduinoJson doesn't read past the buffer end.
  DynamicJsonDocument json(1024);
  auto deserialize_error = deserializeJson(json, payload, len);

  // All mutations below touch shared state (display config, scroll_next,
  // FastLED) that the main loop also reads/writes. Serialize via the
  // shared-state mutex.
  StateLock lock;
  if (!deserialize_error) {
    if (str_topic == mqtt_command_topic) {
      // Home Assistant-style commands
      if (json.containsKey("state")) {
        enable_display = json["state"].as<String>() == "ON";
      }
      if (json.containsKey("brightness")) {
        display_manager->SetBrightness(json["brightness"]);
      }
      if (json.containsKey("color")) {
        layout->text().SetColorRgb(json["color"]["r"], json["color"]["g"],
                                   json["color"]["b"]);
      }
    } else if (str_topic == mqtt_node_topic + "/text") {
      if (json.containsKey("text")) {
        const std::string text = led_marquee::Interpolate(json["text"]);
        const String str(text.data(), text.length());
        if (json.containsKey("scroll") && json["scroll"] == false) {
          layout->text().ShowStaticText(str);
        } else {
          scroll_next = str;
          layout->text().EnableScrolling();
        }
      } else {
        debug_println("missing key 'text'");
      }
    } else if (str_topic == mqtt_node_topic + "/display") {
      if (json.containsKey("enabled")) {
        enable_display = json["enabled"];
      }
      if (json.containsKey("speed")) {
        scroll_speed = json["speed"];
        scroll_timer->setPeriod(scroll_speed);
      }
      if (json.containsKey("color")) {
        String rgb = json["color"];
        unsigned long rgbl = strtoul(rgb.c_str(), NULL, 16);
        uint8_t b = rgbl & 0xff;
        uint8_t g = (rgbl >> 8) & 0xff;
        uint8_t r = (rgbl >> 16) & 0xff;
        layout->text().SetColorRgb(r, g, b);
      }
    } else if (str_topic == mqtt_node_topic + "/ota") {
      if (json.containsKey("enabled")) {
        enable_ota = json["enabled"];
      }
    } else {
      debug_print("Unknown topic: ");
      debug_println(topic);
    }
  } else {
    debug_print("failed to parse json payload: ");
    debug_println(deserialize_error.c_str());
  }
}

void OnMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  debug_println("Disconnected from MQTT.");

  xTimerStart(mqtt_reconnect_timer, 0);
}

void InitMqtt() {
  const char* mqtt_host = config.StringValue("mqtt_host");
  if (!strlen(mqtt_host)) return;

  const char* mqtt_user = config.StringValue("mqtt_user");
  const char* mqtt_pass = config.StringValue("mqtt_pass");

  debug_printf("MQTT: host=%s user=%s port=%d\n", mqtt_host, mqtt_user,
               config.IntValue("mqtt_port"));

  mqtt_reconnect_timer =
      xTimerCreate("mqtt_timer", pdMS_TO_TICKS(2000), pdFALSE, (void*)0,
                   reinterpret_cast<TimerCallbackFunction_t>(ConnectToMqtt));

  mqtt_client.onConnect(OnMqttConnect);
  mqtt_client.onDisconnect(OnMqttDisconnect);
  mqtt_client.onMessage(OnMqttMessage);
  mqtt_client.setServer(mqtt_host, config.IntValue("mqtt_port"));
  mqtt_client.setCredentials(mqtt_user, mqtt_pass);

  ConnectToMqtt();
}

void InitTime() { configTzTime(kTimeZone, kNtpServer); }

void InitWebServer() {
  server.serveStatic("/", *web_fs, "/www/").setDefaultFile("index.html");

  // All these handlers run on the AsyncTCP task; take the state mutex while
  // mutating display state shared with the main loop.
  server.on("/text", HTTP_POST, [](AsyncWebServerRequest* request) {
    {
      StateLock lock;
      if (auto param_text = request->getParam("text", true)) {
        if (request->getParam("do_queue", true))
          scroll_next = param_text->value();
        else
          layout->text().ShowScrollText(param_text->value());
      }
    }
    request->redirect("/");
  });

  server.on("/color", HTTP_POST, [](AsyncWebServerRequest* request) {
    {
      StateLock lock;
      if (auto param_color = request->getParam("color", true)) {
        String color = param_color->value();
        if (color.length() == 7) {
          unsigned long rgbl = strtoul(color.c_str() + 1, NULL, 16);
          uint8_t b = rgbl & 0xff;
          uint8_t g = (rgbl >> 8) & 0xff;
          uint8_t r = (rgbl >> 16) & 0xff;
          layout->text().SetColorRgb(r, g, b);
        }
      }
    }
    request->redirect("/");
  });

  server.on("/brightness", HTTP_POST, [](AsyncWebServerRequest* request) {
    {
      StateLock lock;
      if (auto param_brightness = request->getParam("brightness", true)) {
        display_manager->SetBrightness(param_brightness->value().toInt());
      }
    }
    request->redirect("/");
  });

  server.on("/speed", HTTP_POST, [](AsyncWebServerRequest* request) {
    {
      StateLock lock;
      if (auto param_speed = request->getParam("speed", true)) {
        scroll_speed = param_speed->value().toInt();
        scroll_timer->setPeriod(scroll_speed);
      }
    }
    request->redirect("/");
  });

  server.onNotFound([](AsyncWebServerRequest* request) {
    auto* response = request->beginResponse(*web_fs, "/www/404.html");
    response->setCode(404);
    request->send(response);
  });

  server.begin();
}

// Called when we're fully connected and ready to start the show
void InitMain() {
  InitWebServer();

  InitMqtt();

  layout->text().ShowScrollText(kStartupMessage);
}

void InitArduinoOTA() {
  const char* hostname = config.StringValue("hostname");
  if (strlen(hostname)) ArduinoOTA.setHostname(hostname);

  ArduinoOTA.setPartitionLabel(&kSpiffsFsLabel[1]);

  static led_marquee::TextLayout ota_message(*display_manager, MatriseFontData,
                                             1);

  // ArduinoOTA.handle() runs the whole transfer and flash write inside a
  // single call from loop(), so loop() doesn't get to feed the task watchdog
  // until the update is done. Any update longer than kWatchdogTimeoutSec was
  // being cut off by a TASK_WDT reset. Feed it from the progress callback,
  // which fires per received chunk; a transfer that actually stalls still
  // gets reset, which is what we want.
  ArduinoOTA.onStart([]() {
    led_marquee::SetBreadcrumb("ota_update");
    led_marquee::FeedWatchdog();
    ota_message.text().SetColorRgb(0xff, 0xff, 0x00);
    ota_message.text().SetBackgroundMode(BACKGND_LEAVE);
    FastLED.clear();
    ota_message.text().ShowStaticText("OTA UPDATE");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    char progress_text[22];

    led_marquee::FeedWatchdog();

    EVERY_N_SECONDS(1) {
      float pct = static_cast<float>(progress) / static_cast<float>(total);
      uint16_t w = display_manager->GetWidth() - 1;

      snprintf(progress_text, sizeof(progress_text), "OTA UPDATE: %u%%",
               int(100.0 * pct));

      FastLED.clear();
      display_manager->FillArea(0, 0, w * pct, 0, CRGB::DarkGreen);
      ota_message.text().ShowStaticText(progress_text);

      FastLED.show();
    }
  });

  // ArduinoOTA restarts the chip itself after a successful update; don't let
  // the next boot report that as a crash.
  ArduinoOTA.onEnd([]() { led_marquee::NoteCleanRestart("ota_update"); });

  ArduinoOTA.onError([](ota_error_t error) {
    const char* error_text;
    switch (error) {
      case OTA_AUTH_ERROR:
        error_text = "OTA_AUTH_ERROR";
        break;
      case OTA_BEGIN_ERROR:
        error_text = "OTA_BEGIN_ERROR";
        break;
      case OTA_CONNECT_ERROR:
        error_text = "OTA_CONNECT_ERROR";
        break;
      case OTA_RECEIVE_ERROR:
        error_text = "OTA_RECEIVE_ERROR";
        break;
      case OTA_END_ERROR:
        error_text = "OTA_END_ERROR";
        break;
      default:
        error_text = "UNKNOWN OTA ERROR";
    }

    FastLED.clear();
    ota_message.text().ShowStaticText(error_text);

    PlannedRestart("ota_error", nullptr, 5000);
  });

  ArduinoOTA.begin();
}

// If WiFi has been down for too long, reboot. WiFi failures can manifest as
// any of WL_DISCONNECTED, WL_NO_SSID_AVAIL, WL_CONNECTION_LOST,
// WL_IDLE_STATUS, or WL_CONNECT_FAILED, so we treat anything other than
// WL_CONNECTED as a problem. The reboot fires only once we've been
// disconnected for several checks in a row, to ride out brief blips.
//
// Runs every 5 seconds (from EVERY_N_SECONDS(5) in loop()), so a count
// threshold of 3 means roughly 15 seconds of unbroken disconnection before we
// reboot. We don't trigger before the initial connection has succeeded, so
// the user has time to use the config portal on a brand-new device.
void RebootIfDisconnected(byte& disconnect_count) {
  if (!is_connected || wm->getConfigPortalActive()) {
    disconnect_count = 0;
    return;
  }

  wl_status_t status = WiFi.status();
  if (status != WL_CONNECTED) {
    disconnect_count++;
    debug_println(String("WiFi not connected: status=") +
                  static_cast<int>(status) +
                  " count=" + static_cast<int>(disconnect_count));
    if (disconnect_count > 3) {
      debug_println("Rebooting due to sustained WiFi disconnection");
      PlannedRestart("wifi_lost", "DISCONNECTED", 3000);
    }
  } else {
    disconnect_count = 0;
  }
}

// Detect when WiFi has come up, and complete initialization
void CheckForStartup() {
  if (is_connected || wm->getConfigPortalActive() ||
      WiFi.status() != WL_CONNECTED) {
    return;
  }

  // The auto-opened portal has closed on its own (WiFiManager's own timeout,
  // or a successful save through it) and STA is up. Don't try to start the
  // show from here: ConfigModeCallback replaced the layout with a clockless
  // one and there's no way back, and a fresh boot with the router reachable
  // connects straight away. This also removes a race with the 5-minute cap
  // below, which previously decided between "start clockless" and "reboot"
  // depending on where the 5-second tick landed. A pending
  // SaveConfigAndRestart() runs earlier in loop(), so a real configuration
  // session isn't cut short.
  if (auto_config_entered_ms != 0) {
    PlannedRestart("auto_cfg_closed", "RETRY");
    return;
  }

  is_connected = true;
  config_mode = false;
  debug_println("Starting up");

  InitMain();
}

// If WiFiManager auto-opened its config portal because the saved credentials
// failed at boot, give a human a reasonable window to walk over and configure
// it, then reboot. By the time we get here, WiFi may well have come back
// (e.g., the router has finished rebooting), and a fresh autoConnect will
// succeed. Without this the marquee can sit displaying "Connect to ... to
// configure" indefinitely.
//
// Deliberately doesn't look at is_connected: nothing legitimate ever clears
// auto_config_entered_ms, so if we've been through the auto portal this boot,
// a reboot is the only clean way out no matter what else happened.
void RebootIfStuckInAutoConfig() {
  if (auto_config_entered_ms == 0) return;  // not in auto config mode
  if (millis() - auto_config_entered_ms < kAutoConfigRebootMs) return;

  debug_println("Auto-config portal timed out; rebooting to retry WiFi");
  PlannedRestart("auto_cfg_timeout", "RETRY");
}

// Same idea for the reset-pin web portal, which otherwise has no exit but a
// save or a power cycle.
void RebootIfStuckInButtonConfig() {
  if (button_config_entered_ms == 0) return;
  if (millis() - button_config_entered_ms < kButtonConfigRebootMs) return;

  debug_println("Button config portal timed out; rebooting");
  PlannedRestart("button_cfg_timeout", "RETRY");
}

// The failure we've actually seen on the wall and can't derive from the code:
// the marquee is online (MQTT up, answering commands) yet still scrolling the
// "Connect to ... to configure." prompt. No code path we can find sets that
// text after startup, so rather than assume, detect it: if we consider
// ourselves started and the scroller still holds the prompt, publish
// everything we know immediately (so the retained snapshot survives), leave
// it up for a while in case someone wants to poke at it live, then reboot.
void RecoverIfPromptStuck() {
  if (!is_connected || config_prompt.isEmpty()) return;

  bool stuck;
  {
    StateLock lock;
    stuck = layout->text().ScrollBuffer().endsWith(config_prompt);
  }
  if (!stuck) {
    prompt_stuck_since_ms = 0;
    return;
  }

  if (prompt_stuck_since_ms == 0) {
    prompt_stuck_since_ms = millis();
    if (prompt_stuck_since_ms == 0) prompt_stuck_since_ms = 1;
    debug_println("Config prompt still on screen while connected!");
    led_marquee::SetBreadcrumb("prompt_stuck");
    PublishDiag("prompt_stuck", "anomaly");
    return;
  }

  if (millis() - prompt_stuck_since_ms < kAutoConfigRebootMs) return;
  PlannedRestart("prompt_stuck", "RETRY");
}

// Enter WiFiManager's web portal (STA mode, no AP) for runtime
// reconfiguration. There is no way back to normal operation except a reboot,
// which RebootIfStuckInButtonConfig() or a save will provide.
void EnterButtonConfig() {
  config_mode = true;
  button_config_entered_ms = millis();
  if (button_config_entered_ms == 0) button_config_entered_ms = 1;
  led_marquee::SetBreadcrumb("button_config_portal");
  {
    StateLock lock;
    RemoveClock();
    layout->text().ShowScrollText("CONFIG: http://" +
                                  WiFi.localIP().toString());
    PublishDiag("button_config");
  }
  debug_println("Enter WebPortal");
  server.end();
  wm->setParamsPage(true);
  wm->startWebPortal();
}

void setup() {
  // First thing: capture reset reason / crash log from the previous run.
  // Anything that crashes after this point will be reported on next boot.
  led_marquee::InitSystemHealth();
  pending_crash_report = led_marquee::GetCrashReport();
  led_marquee::SetBreadcrumb("setup");

  // Create the shared-state mutex before any code path that could mutate
  // shared state from another task (MQTT/HTTP callbacks, FreeRTOS timers).
  g_state_mutex = xSemaphoreCreateRecursiveMutex();

  WiFi.mode(WIFI_STA);  // explicitly set mode, esp defaults to STA+AP

  pinMode(kResetPin, INPUT_PULLUP);

  debug_begin(115200);
  debug_setDebugOutput(true);

  InitLEDs();

  CheckForResetConfig();

  layout->text().ShowStaticText("START");

  config.AddParam("hostname", "mDNS hostname", "", 63);
  config.AddHtml("<hr /><p>Leave MQTT host blank to disable MQTT.</p>");
  config.AddParam("mqtt_host", "MQTT host", "", 63);
  config.AddParam("mqtt_port", "MQTT port", "1883", 5);
  config.AddParam("mqtt_user", "MQTT user", "", 16);
  config.AddParam("mqtt_pass", "MQTT password", "", 16);
  config.AddHtml(
      "<p>Unique identifier for this node. Will receive events "
      "under <i>" +
      String(kMqttPrefix) + "/&lt;node name&gt;</i> topic.</p>");
  config.AddParam("mqtt_node", "MQTT node name", "marquee", 16);

  web_fs = GetFileSystem(kSpiffsFsLabel);

  LoadUserConfig();

  scroll_timer = new CEveryNMillis(scroll_speed);

  SetupWiFiManager();

  // Count STA disconnects (and remember the last reason) for the diag
  // snapshot. Runs on the WiFi event task; plain integer stores only.
  WiFi.onEvent(
      [](WiFiEvent_t event, WiFiEventInfo_t info) {
        wifi_disconnect_events++;
        wifi_last_disconnect_reason = info.wifi_sta_disconnected.reason;
        wifi_last_disconnect_s = millis() / 1000UL;
      },
      ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  debug_println("Connecting to WiFi...");
  unsigned long autoconnect_start = millis();
  autoconnect_ok = wm->autoConnect(kSetupAp);
  autoconnect_ms = millis() - autoconnect_start;
  debug_println(autoconnect_ok ? "Connected" : "Connection failed");

  InitTime();

  InitArduinoOTA();

  // Subscribe the loop task to the Task Watchdog Timer. From here on, if
  // loop() doesn't run for kWatchdogTimeoutSec, the chip resets.
  led_marquee::InitWatchdog(kWatchdogTimeoutSec);
  led_marquee::SetBreadcrumb("setup_done");
}

void loop() {
  static byte disconnectCount = 0;

  // Pet the watchdog every iteration. If we hang anywhere in loop() or in a
  // call from loop(), no reset means trouble.
  led_marquee::FeedWatchdog();

  // Run asynchronous WiFi Manager
  if (config_mode == true || is_connected == false) wm->process();

  if (should_save_config) SaveConfigAndRestart();

  // Run asynchronous OTA receiver
  if (enable_ota) ArduinoOTA.handle();

  // Do the scrolling. Hold the state mutex for the duration of an animate +
  // show cycle so MQTT / HTTP callbacks can't reconfigure the cLEDText state
  // machine or repaint pixels mid-flight.
  if (*scroll_timer) {
    StateLock lock;
    if (enable_display) {
      AnimateScroller();
      FastLED.show();
    } else {
      FastLED.clear(true);
    }
  } else if (enable_clock) {
    EVERY_N_SECONDS(1) {
      StateLock lock;
      clock_hue++;
      SetClockColor();

      time_t now = time(NULL);
      tm* timeinfo = localtime(&now);
      char t[40];
      strftime(t, sizeof(t), "%l:%M:%S", timeinfo);
      layout->clock().SetText(t);
    }
  }

  // Periodic housekeeping. Run every 5 seconds to not waste CPU.
  EVERY_N_SECONDS(5) {
    RebootIfDisconnected(disconnectCount);
    RebootIfStuckInAutoConfig();
    RebootIfStuckInButtonConfig();
    CheckForStartup();
    RecoverIfPromptStuck();
    // Update the breadcrumb periodically so a crash report includes a recent
    // sense of where we were, the uptime, and a heap snapshot.
    char crumb[64];
    snprintf(crumb, sizeof(crumb), "loop up=%lus ip=%s heap=%u",
             millis() / 1000UL, WiFi.localIP().toString().c_str(),
             (unsigned)ESP.getFreeHeap());
    led_marquee::SetBreadcrumb(crumb);
  }

  // Retained heartbeat with the full state, so the last minute before any
  // failure is on the broker.
  if (kDiagIntervalSec > 0) {
    EVERY_N_SECONDS(kDiagIntervalSec) { PublishDiag("periodic"); }
  }

  // If the reset pin is held low during operation, enter WiFi Manager config.
  // Requires kResetHoldMs of continuous LOW; any HIGH sample restarts the
  // count.
  if (config_mode == false) {
    static bool reset_low = false;
    static unsigned long reset_low_since = 0;
    if (digitalRead(kResetPin) == LOW) {
      if (!reset_low) {
        reset_low = true;
        reset_low_since = millis();
      } else if (millis() - reset_low_since >= kResetHoldMs) {
        reset_low = false;
        EnterButtonConfig();
      }
    } else {
      reset_low = false;
    }
  }
}
