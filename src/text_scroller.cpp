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

#include "text_scroller.h"

#include <FastLED.h>
#include <FontClassic.h>
#include <LEDText.h>
#include <string.h>

#include <cstddef>

#include "display_manager.h"

namespace led_marquee {

TextScroller::TextScroller(DisplayManager &display_manager,
                           const uint8_t *font_data)
    : display_manager_(display_manager) {
  led_text_.SetFont(font_data);
  led_text_.SetTextColrOptions(COLR_RGB | COLR_SINGLE, 0xff, 0xff, 0xff);
}

void TextScroller::Init(const int width, const int height, const int x,
                        const int y) {
  width_ = width;
  height_ = height;
  x_ = x;
  y_ = y;

  display_manager_.InitLedText(led_text_, width, height, x, y);

  std::size_t num_spaces = 1 + width / (led_text_.FontWidth() + 1);
  char *buf = new char[num_spaces + 1];
  memset(buf, ' ', num_spaces);
  buf[num_spaces] = '\0';

  spaces_ = buf;
}

void TextScroller::SetColorRgb(uint8_t r, uint8_t g, uint8_t b) {
  led_text_.SetTextColrOptions(COLR_RGB | COLR_SINGLE, r, g, b);
}

void TextScroller::SetBackgroundMode(uint16_t options, uint8_t dimming) {
  led_text_.SetBackgroundMode(options, dimming);
}

void TextScroller::ShowStaticText(const String &text) {
  scroll_mode_ = ScrollMode::kStatic;

  auto len = text.length();
  if (len > max_length_) {
    Serial.print("WARNING: Message truncated (");
    Serial.print(len);
    Serial.print(" > ");
    Serial.print(max_length_);
    Serial.println(")");
  }

  String message = text.substring(0, max_length_);

  if (display_manager_.IsEnabled()) {
    EraseArea();
    led_text_.SetScrollDirection(SCROLL_LEFT);
    led_text_.SetText((unsigned char *)message.c_str(), message.length());
    led_text_.UpdateText();
    FastLED.show();
  }
}

// `text` must be NULL-terminated
void TextScroller::ShowScrollText(const String &text) {
  scroll_mode_ = ScrollMode::kScrolling;

  auto len = text.length();
  if (len > max_length_) {
    Serial.print("WARNING: Message truncated (");
    Serial.print(len);
    Serial.print(" > ");
    Serial.print(max_length_);
    Serial.println(")");
  }

  scroll_buf_ = spaces_ + text.substring(0, max_length_);

  ShowScrollText();
}

void TextScroller::ShowScrollText() {
  led_text_.SetScrollDirection(SCROLL_LEFT);
  led_text_.SetText((unsigned char *)scroll_buf_.c_str(), scroll_buf_.length());
}

void TextScroller::EraseArea() {
  display_manager_.FillArea(x_, y_, width_, height_);
}

bool TextScroller::Animate() {
  if (scroll_mode_ == TextScroller::ScrollMode::kScrolling) {
    return led_text_.UpdateText() != -1;
  }

  return true;
}

}  // namespace led_marquee