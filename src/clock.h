/*
 * Copyright 2022 Google LLC
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

#ifndef LED_MARQUEE_CLOCK_H_
#define LED_MARQUEE_CLOCK_H_

#include <LEDText.h>

#include <memory>

#include "display_manager.h"

namespace led_marquee {

class Clock {
 public:
  Clock(DisplayManager &display_manager, const uint8_t *font_data);

  Clock(const Clock &other) = default;
  Clock &operator=(const Clock &other) = default;

  void Init(const int width, const int height, const int x, const int y);

  uint8_t FontHeight() { return led_text_.FontHeight(); };

  void SetColorHsv(uint8_t hue, uint8_t saturation, uint8_t value);
  void SetText(const String &text);
  void EraseArea();

 private:
  DisplayManager &display_manager_;
  cLEDText led_text_;

  int width_, height_, x_, y_;
};

}  // namespace led_marquee

#endif  // LED_MARQUEE_CLOCK_H_
