// Copyright 2024 Christopher Masto
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

#include "life_with_clock_layout.h"

#include <stdint.h>

namespace led_marquee {

LifeWithClockLayout::LifeWithClockLayout(DisplayManager &display_manager,
                                         const uint8_t *text_font,
                                         int clock_width,
                                         const uint8_t *clock_font,
                                         int text_width)
    : display_manager_(display_manager),
      life_(display_manager),
      clock_(display_manager, clock_font),
      text_scroller_(display_manager, text_font) {
  const auto width = display_manager_.GetWidth();
  const auto height = display_manager_.GetHeight();
  const auto life_width = width - clock_width;

  // Special-case takeover screen for configuration mode
  if (text_width > 0) {
    auto text_height = text_scroller_.FontHeight() + 1;
    text_scroller_.Init(text_width, text_height, 0, 0);
    return;
  };

  if (life_width > 0) {
    life_.Init(display_manager_.Leds(), life_width, height, 0, 0);
  }

  if (clock_width > 0) {
    auto clock_height = clock_.FontHeight() + 1;
    clock_.Init(clock_width - 1, clock_height, width - clock_width + 1, 0);
  }
}

}  // namespace led_marquee
