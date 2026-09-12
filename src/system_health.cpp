// Copyright 2025 Christopher Masto
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

#include "system_health.h"

#include <Arduino.h>
#include <esp_system.h>
#include <esp_task_wdt.h>

#include <cstring>

#include "debug_serial.h"

namespace led_marquee {

namespace {

// Bump the magic whenever PersistentState's layout changes, so a firmware
// update doesn't misread the previous image's state.
constexpr uint32_t kStateMagic = 0xC0FFEEEFu;
constexpr uint32_t kCleanMarker = 0xDEADBEEFu;
constexpr size_t kBreadcrumbSize = 96;
constexpr size_t kRestartReasonSize = 32;

// RTC_NOINIT memory is preserved across software resets (including panics
// and watchdog resets) but its contents are undefined after a hard power
// cycle, hence the magic field for validation.
struct PersistentState {
  uint32_t magic;
  uint32_t clean_marker;
  uint32_t boot_count;
  uint32_t last_reason;    // esp_reset_reason_t cast to uint32_t
  uint32_t last_uptime_s;  // uptime when NoteCleanRestart() was called
  char breadcrumb[kBreadcrumbSize];
  char restart_reason[kRestartReasonSize];  // set by NoteCleanRestart()
};

RTC_NOINIT_ATTR PersistentState g_state;

String g_crash_report;
String g_last_restart;
esp_reset_reason_t g_reset_reason = ESP_RST_UNKNOWN;
uint32_t g_boot_count = 0;

const char* ResetReasonString(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_UNKNOWN:
      return "UNKNOWN";
    case ESP_RST_POWERON:
      return "POWERON";
    case ESP_RST_EXT:
      return "EXT";
    case ESP_RST_SW:
      return "SW";
    case ESP_RST_PANIC:
      return "PANIC";
    case ESP_RST_INT_WDT:
      return "INT_WDT";
    case ESP_RST_TASK_WDT:
      return "TASK_WDT";
    case ESP_RST_WDT:
      return "WDT";
    case ESP_RST_DEEPSLEEP:
      return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:
      return "BROWNOUT";
    case ESP_RST_SDIO:
      return "SDIO";
    default:
      return "OTHER";
  }
}

}  // namespace

void InitSystemHealth() {
  esp_reset_reason_t reason = esp_reset_reason();
  g_reset_reason = reason;

  bool magic_valid = (g_state.magic == kStateMagic);
  bool was_clean = magic_valid && g_state.clean_marker == kCleanMarker;

  // Carry boot count forward, or start fresh on cold boot.
  uint32_t prev_boot_count = magic_valid ? g_state.boot_count : 0;
  g_boot_count = prev_boot_count + 1;

  // Build crash report for the *previous* run.
  // POWERON is always considered clean (cold boot).
  // SW reset is clean only if we explicitly marked it.
  if (reason == ESP_RST_POWERON || (reason == ESP_RST_SW && was_clean)) {
    g_crash_report = "";
  } else {
    g_crash_report = String("Reboot reason: ") + ResetReasonString(reason);
    if (magic_valid && g_state.breadcrumb[0] != '\0') {
      // Make sure breadcrumb is null-terminated even if memory was corrupted.
      g_state.breadcrumb[kBreadcrumbSize - 1] = '\0';
      g_crash_report += " | last: ";
      g_crash_report += g_state.breadcrumb;
    }
    g_crash_report += " | boot#";
    g_crash_report += g_boot_count;
  }

  // Describe how the previous run ended, clean or not, for the diagnostic
  // snapshot. Planned restarts leave a reason and uptime behind; crashes
  // leave a breadcrumb.
  g_last_restart = ResetReasonString(reason);
  if (was_clean) {
    g_state.restart_reason[kRestartReasonSize - 1] = '\0';
    if (g_state.restart_reason[0] != '\0') {
      g_last_restart += " (";
      g_last_restart += g_state.restart_reason;
      g_last_restart += ")";
    }
    g_last_restart += " after ";
    g_last_restart += g_state.last_uptime_s;
    g_last_restart += "s";
  } else if (magic_valid && g_state.breadcrumb[0] != '\0') {
    g_state.breadcrumb[kBreadcrumbSize - 1] = '\0';
    g_last_restart += " | last: ";
    g_last_restart += g_state.breadcrumb;
  }

  // Reset persistent state for the *current* run.
  g_state.magic = kStateMagic;
  g_state.clean_marker = 0;  // assume not clean unless told otherwise
  g_state.boot_count = g_boot_count;
  g_state.last_reason = static_cast<uint32_t>(reason);
  g_state.last_uptime_s = 0;
  g_state.breadcrumb[0] = '\0';
  g_state.restart_reason[0] = '\0';

  debug_printf("[health] boot#%u reset_reason=%s prev_clean=%d\n",
               (unsigned)g_boot_count, ResetReasonString(reason),
               was_clean ? 1 : 0);
  if (g_crash_report.length()) {
    debug_print("[health] crash report: ");
    debug_println(g_crash_report);
  }
}

String GetCrashReport() { return g_crash_report; }

String GetLastRestartInfo() { return g_last_restart; }

const char* GetResetReasonName() { return ResetReasonString(g_reset_reason); }

uint32_t GetBootCount() { return g_boot_count; }

void SetBreadcrumb(const char* info) {
  if (info == nullptr) {
    g_state.breadcrumb[0] = '\0';
    return;
  }
  // strlcpy guarantees null-termination and bounds.
  strlcpy(g_state.breadcrumb, info, kBreadcrumbSize);
}

void NoteCleanRestart(const char* reason) {
  g_state.clean_marker = kCleanMarker;
  g_state.last_uptime_s = millis() / 1000UL;
  strlcpy(g_state.restart_reason, reason ? reason : "", kRestartReasonSize);
}

void InitWatchdog(uint32_t timeout_seconds) {
  // Subscribe the calling task (the Arduino loop task when called from
  // setup()) to the Task Watchdog Timer. panic=true => reset on timeout.
  // esp_task_wdt_init returns ESP_OK if newly initialized,
  // ESP_ERR_INVALID_STATE if already initialized (which is fine; we still need
  // to add ourselves).
  esp_err_t init_err = esp_task_wdt_init(timeout_seconds, true);
  if (init_err != ESP_OK && init_err != ESP_ERR_INVALID_STATE) {
    debug_printf("[health] esp_task_wdt_init failed: %d\n", init_err);
  }
  esp_err_t add_err = esp_task_wdt_add(NULL);
  if (add_err != ESP_OK && add_err != ESP_ERR_INVALID_ARG) {
    // ESP_ERR_INVALID_ARG = task already subscribed; harmless.
    debug_printf("[health] esp_task_wdt_add failed: %d\n", add_err);
  }
  debug_printf("[health] watchdog armed: %us\n", (unsigned)timeout_seconds);
}

void FeedWatchdog() { esp_task_wdt_reset(); }

}  // namespace led_marquee
