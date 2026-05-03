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
// We use a single recursive-ish protection pattern: any code that touches
// shared state (animation, scroll_next, layout->text() mutating calls,
// FastLED.show()) takes this mutex first. Critical sections are short.
SemaphoreHandle_t g_state_mutex = nullptr;

// RAII helper for the state mutex.
class StateLock {
 public:
  StateLock() {
    if (g_state_mutex) xSemaphoreTake(g_state_mutex, portMAX_DELAY);
  }
  ~StateLock() {
    if (g_state_mutex) xSemaphoreGive(g_state_mutex);
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

String mqtt_node_topic;
String mqtt_command_topic;
String mqtt_ready_topic;

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
        delay(1000);
        led_marquee::NoteCleanRestart();
        wm->reboot();
      }
    }
  }
}

// Copy config from the user and flag it to be stored to the filesystem
void SaveParamsCallback() {
  should_save_config = true;

  config.ReadFromWifiManager();
}

// When WiFiManager enters configuration mode, display a prompt
void ConfigModeCallback(WiFiManager* myWiFiManager) {
  config_mode = true;
  RemoveClock();

  layout->text().ShowScrollText(
      "Connect to " + myWiFiManager->getConfigPortalSSID() + " to configure.");
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
    delay(1000);
    led_marquee::NoteCleanRestart();
    wm->reboot();
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

  delay(1000);
  led_marquee::NoteCleanRestart();
  ESP.restart();
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

  mqtt_node_topic = String(kMqttPrefix) + "/" + config.StringValue("mqtt_node");
  mqtt_command_topic = mqtt_node_topic + "/set";
  mqtt_ready_topic = mqtt_node_topic + "/ready";

  String mqtt_subscription = mqtt_node_topic + "/#";
  mqtt_client.subscribe(mqtt_subscription.c_str(), 0);
  debug_println("Subscribed to " + mqtt_subscription);

  MqttDiscovery();

  // Publish a status payload describing this boot, including any crash report
  // from the previous run. Topic: <prefix>/<node>/status, retained so a
  // subscriber that connects later still gets the most recent boot info.
  {
    StaticJsonDocument<384> status;
    status["boot"] = led_marquee::GetBootCount();
    status["uptime_s"] = millis() / 1000UL;
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
    } else if (str_topic == mqtt_node_topic + "/ready") {
      // Ignore our own messages
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

  ArduinoOTA.onStart([]() {
    ota_message.text().SetColorRgb(0xff, 0xff, 0x00);
    ota_message.text().SetBackgroundMode(BACKGND_LEAVE);
    FastLED.clear();
    ota_message.text().ShowStaticText("OTA UPDATE");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    char progress_text[22];

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

    delay(5000);
    led_marquee::NoteCleanRestart();
    ESP.restart();
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
      layout->text().ShowStaticText("DISCONNECTED");
      delay(3000);
      led_marquee::NoteCleanRestart();
      ESP.restart();
    }
  } else {
    disconnect_count = 0;
  }
}

// Detect when WiFi has come up, and complete initialization
void CheckForStartup() {
  if (is_connected == false && wm->getConfigPortalActive() == false &&
      WiFi.status() == WL_CONNECTED) {
    is_connected = true;
    config_mode = false;
    debug_println("Starting up");

    InitMain();
  }
}

void setup() {
  // First thing: capture reset reason / crash log from the previous run.
  // Anything that crashes after this point will be reported on next boot.
  led_marquee::InitSystemHealth();
  pending_crash_report = led_marquee::GetCrashReport();
  led_marquee::SetBreadcrumb("setup");

  // Create the shared-state mutex before any code path that could mutate
  // shared state from another task (MQTT/HTTP callbacks, FreeRTOS timers).
  g_state_mutex = xSemaphoreCreateMutex();

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

  debug_println("Connecting to WiFi...");
  bool res = wm->autoConnect(kSetupAp);
  debug_println(res ? "Connected" : "Connection failed");

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
    CheckForStartup();
    // Update the breadcrumb periodically so a crash report includes a recent
    // sense of where we were and a heap snapshot.
    char crumb[64];
    snprintf(crumb, sizeof(crumb), "loop ip=%s heap=%u",
             WiFi.localIP().toString().c_str(), (unsigned)ESP.getFreeHeap());
    led_marquee::SetBreadcrumb(crumb);
  }

  // If reset pin is pulled low during operation, enter WiFi Manager config
  if (config_mode == false && digitalRead(kResetPin) == LOW) {
    delay(50);
    if (digitalRead(kResetPin) == LOW) {
      config_mode = true;
      RemoveClock();
      layout->text().ShowScrollText("CONFIG: http://" +
                                    WiFi.localIP().toString());
      debug_println("Enter WebPortal");
      server.end();
      wm->setParamsPage(true);
      wm->startWebPortal();
    }
  }
}
