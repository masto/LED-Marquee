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

#include "text_with_life_layout.h"

#include <stdint.h>

namespace led_marquee {

TextWithLifeLayout::TextWithLifeLayout(DisplayManager &display_manager,
                                       const uint8_t *font, int life_width)
    : display_manager_(display_manager),
      text_scroller_(display_manager, font),
      life_(display_manager) {
  auto width = display_manager_.GetWidth();
  auto height = display_manager_.GetHeight();

  auto text_height = text_scroller_.FontHeight() + 1;
  // There needs to be room for a 1-pixel space between the text and life
  if (life_width < width - 1) {
    // But special case life_width == 0 to allow text to take all the space
    text_scroller_.Init(width - life_width - (life_width == 0 ? 0 : 1),
                        text_height, 0, 0);
  }

  if (life_width > 0) {
    life_.Init(display_manager_.Leds(), life_width, height, width - life_width,
               0);
  }
}

}  // namespace led_marquee
