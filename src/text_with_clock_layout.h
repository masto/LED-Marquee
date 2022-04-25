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

#ifndef LED_MARQUEE_TEXT_WITH_CLOCK_LAYOUT_H_
#define LED_MARQUEE_TEXT_WITH_CLOCK_LAYOUT_H_

#include <stdint.h>

#include <memory>

#include "clock.h"
#include "display_manager.h"
#include "text_scroller.h"

namespace led_marquee {

class TextWithClockLayout {
 public:
  TextWithClockLayout(DisplayManager &display_manager, const uint8_t *font,
                      int clock_width, const uint8_t *clock_font);

  // Not copyable or movable
  TextWithClockLayout(const TextWithClockLayout &) = delete;
  TextWithClockLayout &operator=(const TextWithClockLayout &) = delete;

  TextScroller &text() { return text_scroller_; };
  Clock &clock() { return clock_; };

 private:
  DisplayManager &display_manager_;
  TextScroller text_scroller_;
  Clock clock_;
};

}  // namespace led_marquee

#endif  // LED_MARQUEE_TEXT_WITH_CLOCK_LAYOUT_H_
