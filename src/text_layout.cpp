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

#include "text_layout.h"

#include <stdint.h>

namespace led_marquee {

TextLayout::TextLayout(DisplayManager &display_manager,
                       const uint8_t *font_data, int y_offset)
    : display_manager_(display_manager),
      text_scroller_(display_manager, font_data) {
  auto width = display_manager_.GetWidth();
  auto height = text_scroller_.FontHeight() + 1;

  text_scroller_.Init(width, height, 0, y_offset);
}

}  // namespace led_marquee