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

#include "clock.h"

#include <FastLED.h>
#include <LEDText.h>
#include <string.h>

#include <cstddef>

#include "display_manager.h"

namespace led_marquee {

Clock::Clock(DisplayManager& display_manager, const uint8_t* font_data)
    : display_manager_(display_manager) {
  led_text_.SetFont(font_data);
  led_text_.SetTextColrOptions(COLR_RGB | COLR_SINGLE, 0xff, 0xff, 0xff);
}

void Clock::Init(const int width, const int height, const int x, const int y) {
  width_ = width;
  height_ = height;
  x_ = x;
  y_ = y;

  display_manager_.InitLedText(led_text_, width, height, x, y);
}

void Clock::SetColorHsv(uint8_t hue, uint8_t saturation, uint8_t value) {
  led_text_.SetTextColrOptions(COLR_HSV | COLR_SINGLE, hue, saturation, value);
}

void Clock::SetText(const String& text) {
  if (display_manager_.IsEnabled()) {
    EraseArea();
    led_text_.SetScrollDirection(SCROLL_LEFT);
    led_text_.SetText((unsigned char*)text.c_str(), text.length());
    led_text_.UpdateText();
  }
}

void Clock::EraseArea() { display_manager_.FillArea(x_, y_, width_, height_); }

}  // namespace led_marquee