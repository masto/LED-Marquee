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

#ifndef LED_MARQUEE_INTERPOLATE_H_
#define LED_MARQUEE_INTERPOLATE_H_

#include <string>
#include <string_view>

namespace led_marquee {

std::string Interpolate(std::string_view input);

}  // namespace led_marquee

#endif  // LED_MARQUEE_INTERPOLATE_H_
