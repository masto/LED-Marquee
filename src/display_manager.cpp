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

#include "display_manager.h"

#include <FastLED.h>
#include <LEDText.h>

namespace led_marquee {

void DisplayManager::SetMaxPower(uint8_t volts, uint32_t max_milliamps) {
  FastLED.setMaxPowerInVoltsAndMilliamps(volts, max_milliamps);
}

void DisplayManager::SetBrightness(uint8_t brightness) { FastLED.setBrightness(brightness); }

}  // namespace led_marquee