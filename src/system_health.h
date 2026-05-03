/*
 * Copyright 2025 Christopher Masto
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// System health utilities: task watchdog, reset-reason / crash log capture.
//
// The ESP32 has a Task Watchdog Timer (TWDT). We subscribe the Arduino loop
// task to it; if the loop hangs for longer than the timeout, the chip panics
// and resets. On the next boot, esp_reset_reason() tells us *why* we reset.
//
// We persist a small structure in RTC_NOINIT_ATTR memory which survives
// soft resets (panics, watchdog, brownouts, sw resets) but is randomized
// on a hard power cycle. We use a magic number to detect validity.
//
// Once MQTT comes up, we publish a human-readable "crash report" describing
// the previous reset reason and any breadcrumb that was set, so failures
// can be diagnosed remotely.

#ifndef LED_MARQUEE_SYSTEM_HEALTH_H_
#define LED_MARQUEE_SYSTEM_HEALTH_H_

#include <Arduino.h>

#include <cstdint>

namespace led_marquee {

// Call once, very early in setup() (before anything else can crash).
// Captures esp_reset_reason() and previous breadcrumb, then resets the
// breadcrumb for this run.
void InitSystemHealth();

// Returns a human-readable description of the previous reset.
// Empty if it appears to have been a clean shutdown (cold power on, or a
// software restart that we initiated and announced via NoteCleanRestart()).
String GetCrashReport();

// Bumped each boot. Useful diagnostic to publish.
uint32_t GetBootCount();

// Set a short string that will be reported on the next boot if we crash
// before another breadcrumb overwrites it. Call this at meaningful points
// (e.g. "wifi_ok", "mqtt_connect", etc.). Limited to ~80 chars.
void SetBreadcrumb(const char* info);

// Call before any planned ESP.restart() / wm->reboot() so the next boot
// doesn't report it as an unexpected reset.
void NoteCleanRestart();

// Initialize the Task Watchdog Timer for the current task (call from setup()
// from the loop task) with the given timeout in seconds. If the loop fails
// to call FeedWatchdog() within the timeout, the chip will panic-reset.
void InitWatchdog(uint32_t timeout_seconds);

// Pet the watchdog. Cheap; safe to call every loop iteration.
void FeedWatchdog();

}  // namespace led_marquee

#endif  // LED_MARQUEE_SYSTEM_HEALTH_H_
