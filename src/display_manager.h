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

#ifndef LED_MARQUEE_DISPLAY_MANAGER_H_
#define LED_MARQUEE_DISPLAY_MANAGER_H_

#include <FastLED.h>
#include <LEDMatrix.h>
#include <LEDText.h>
#include <stdint.h>

#include <memory>

namespace led_marquee {

class DisplayManager {
 public:
  // This is not very generalized, but all this use of templates makes it hard
  // to dynamically instantiate everything.
  template <template <uint8_t, EOrder> class chipset, const uint8_t data_pins[],
            EOrder color_order>
  static std::unique_ptr<DisplayManager> Create(
      int num_sections, int section_width, int matrix_height,
      std::shared_ptr<cLEDMatrixBase> leds, bool enable_display) {
    auto display_manager = std::unique_ptr<DisplayManager>(
        new DisplayManager(leds, enable_display));

    // This is particularly ugly. To support more sections, this code needs
    // to be copied and pasted.
    const int section_pixels = section_width * matrix_height;
    FastLED.addLeds<chipset, data_pins[0], color_order>((*leds)[0],
                                                        section_pixels);
    if (num_sections > 1)
      FastLED.addLeds<chipset, data_pins[1], color_order>(
          (*leds)[section_pixels * 1], section_pixels);
    if (num_sections > 2)
      FastLED.addLeds<chipset, data_pins[2], color_order>(
          (*leds)[section_pixels * 2], section_pixels);
    assert(num_sections < 4);

    // For safety, start with everything off and brightness turned down
    FastLED.setBrightness(10);
    FastLED.clear(true);

    return display_manager;
  };

  // Not copyable
  DisplayManager(const DisplayManager& other) = delete;
  DisplayManager& operator=(const DisplayManager& other) = delete;

  void InitLedText(cLEDText& led_text, const int width, const int height,
                   const int x, const int y) {
    led_text.Init(leds_.get(), width, height, x, y);
  };

  int GetWidth() const { return leds_->Width(); };

  bool IsEnabled() const { return enable_display_; };
  void Enable() { enable_display_ = true; };
  void Disable() { enable_display_ = false; };

  void SetMaxPower(const uint8_t volts, const uint32_t max_milliamps);
  void SetBrightness(const uint8_t brightness);

  void FillArea(const int x, const int y, const int width, const int height,
                const CRGB color = CRGB::Black) {
    leds_->DrawFilledRectangle(x, y, x + width - 1, y + height - 1, color);
  };

 private:
  DisplayManager(std::shared_ptr<cLEDMatrixBase> leds, bool enable_display)
      : leds_(leds), enable_display_(enable_display){};

  std::shared_ptr<cLEDMatrixBase> leds_;
  bool enable_display_ = true;
};

}  // namespace led_marquee

#endif  // LED_MARQUEE_DISPLAY_MANAGER_H_
