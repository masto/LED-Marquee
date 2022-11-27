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

#include "interpolate.h"

#include <charconv>
#include <string>
#include <string_view>

namespace led_marquee {

std::string Interpolate(std::string_view input) {
  std::string output;

  while (!input.empty()) {
    auto pos = input.find('{');
    if (pos == std::string_view::npos) {
      // No (more) escapes, so we're done
      output.append(input);
      return output;
    } else {
      // Grab the part of the string before the escape sequence
      output.append(input, 0, pos);
      input.remove_prefix(pos);

      // Does it even look like `{#aabbcc}`?
      if (input.length() >= 9 && input[1] == '#' && input[8] == '}') {
        // Try to convert hex to RGB
        unsigned long rgbl{};
        auto [ptr, err] =
            std::from_chars(input.data() + 2, input.data() + 2 + 6, rgbl, 16);
        if (err == std::errc{} && ptr == input.data() + 2 + 6) {
          // Consume the interpolated string
          input.remove_prefix(9);

          // Put the escape sequence into the output
          uint8_t b = rgbl & 0xff;
          uint8_t g = (rgbl >> 8) & 0xff;
          uint8_t r = (rgbl >> 16) & 0xff;
          output.push_back('\xe0');
          output.push_back(r);
          output.push_back(g);
          output.push_back(b);
        } else {
          // Reject invalid input
          output.push_back(input.front());
          input.remove_prefix(1);
        }
      } else {
        // Not a real escape sequence, so output the '{'
        output.push_back(input.front());
        input.remove_prefix(1);
      }
    }
  }

  return output;
}

}  // namespace led_marquee
