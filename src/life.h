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

#ifndef LED_MARQUEE_LIFE_H_
#define LED_MARQUEE_LIFE_H_

#include <FastLED.h>
#include <LEDMatrix.h>

#include <memory>

#include "display_manager.h"

namespace led_marquee {

class Life {
 public:
  Life(DisplayManager &display_manager);

  Life(const Life &other) = default;
  Life &operator=(const Life &other) = default;

  void Init(std::shared_ptr<cLEDMatrixBase> leds, const int width,
            const int height, const int x, const int y);

  void EraseArea();
  void ClearBoard();
  void DisplayBoard();
  void SetUpBoard(float density);
  void EvolveBoard();
  void Animate();

 private:
  DisplayManager &display_manager_;
  std::shared_ptr<cLEDMatrixBase> leds_;

  int width_, height_, x_, y_;
  int rounds_;
  std::vector<std::vector<uint8_t>> cells_[4];
};

}  // namespace led_marquee

#endif  // LED_MARQUEE_LIFE_H_
