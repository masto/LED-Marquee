/*
 * Copyright 2024 Christopher Masto
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

#ifndef LED_MARQUEE_TEXT_WITH_LIFE_LAYOUT_H_
#define LED_MARQUEE_TEXT_WITH_LIFE_LAYOUT_H_

#include <stdint.h>

#include <memory>

#include "display_manager.h"
#include "life.h"
#include "text_scroller.h"

namespace led_marquee {

class TextWithLifeLayout {
 public:
  TextWithLifeLayout(DisplayManager &display_manager, const uint8_t *font,
                     int life_width);

  // Not copyable or movable
  TextWithLifeLayout(const TextWithLifeLayout &) = delete;
  TextWithLifeLayout &operator=(const TextWithLifeLayout &) = delete;

  TextScroller &text() { return text_scroller_; };
  Life &life() { return life_; };

 private:
  DisplayManager &display_manager_;
  TextScroller text_scroller_;
  Life life_;
};

}  // namespace led_marquee

#endif  // LED_MARQUEE_TEXT_WITH_LIFE_LAYOUT_H_
