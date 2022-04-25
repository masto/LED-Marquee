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

#ifndef LED_MARQUEE_TEXT_SCROLLER_H_
#define LED_MARQUEE_TEXT_SCROLLER_H_

#include <LEDText.h>
#include <stdint.h>

#include <memory>

#include "display_manager.h"

namespace led_marquee {

class TextScroller {
 public:
  TextScroller(DisplayManager &display_manager, const uint8_t *font_data);

  TextScroller(const TextScroller &other) = default;
  TextScroller &operator=(const TextScroller &other) = default;

  void Init(const int width, const int height, const int x, const int y);

  uint8_t FontHeight() { return led_text_.FontHeight(); };

  void SetColorRgb(uint8_t r, uint8_t g, uint8_t b);
  void SetBackgroundMode(uint16_t options, uint8_t dimming = 0x00);
  void SetMaxLength(const int max_length) { max_length_ = max_length; };

  void ShowStaticText(const String &);
  void ShowScrollText(const String &);
  void ShowScrollText();

  void EraseArea();

  bool Animate();

 private:
  enum class ScrollMode { kStatic, kScrolling };

  DisplayManager &display_manager_;
  cLEDText led_text_;
  ScrollMode scroll_mode_ = ScrollMode::kScrolling;

  String spaces_, scroll_buf_;

  int width_, height_, x_, y_;
  int max_length_ = 1024;
};

}  // namespace led_marquee

#endif  // LED_MARQUEE_TEXT_SCROLLER_H_
