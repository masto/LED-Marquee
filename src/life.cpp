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

#include "life.h"

#include <FastLED.h>
#include <LEDMatrix.h>

#include <memory>

#include "display_manager.h"

// "Live" cell colors to randomly choose from
const CRGB kColors[8] = {
    CRGB(0x00, 0xFF, 0x00), CRGB(0x00, 0x00, 0xFF), CRGB(0xFF, 0x00, 0x00),
    CRGB(0xB8, 0xE9, 0x86), CRGB(0xF8, 0xB7, 0x00), CRGB(0x50, 0xE3, 0xC2),
    CRGB(0xFE, 0x13, 0xD4), CRGB(0x90, 0x13, 0xFE),
};

// Fade-out color palette (reverse order). Must contain 9 elements.
// https:  // colordesigner.io/gradient-generator is a good place to make one.
CRGB kFadePalette[9] = {
    CRGB(52, 41, 51),    CRGB(68, 50, 58),
    CRGB(83, 60, 63),    CRGB(96, 71, 67),
    CRGB(106, 84, 71),   CRGB(114, 98, 77),
    CRGB(118, 113, 87),  CRGB(119, 128, 100),
    CRGB(250, 250, 110),  // Gets replaced with one of the live colors above
};

const int kMaxRounds = 200;

namespace led_marquee {

Life::Life(DisplayManager& display_manager)
    : display_manager_(display_manager) {}

void Life::Init(std::shared_ptr<cLEDMatrixBase> leds, const int width,
                const int height, const int x, const int y) {
  leds_ = leds;
  width_ = width;
  height_ = height;
  x_ = x;
  y_ = y;

  SetUpBoard(0.3);
}

void Life::EraseArea() { display_manager_.FillArea(x_, y_, width_, height_); }

// The actual board is inset in a larger board, leaving one cell of padding
// around each edge.
void Life::ClearBoard() {
  cells_[0].assign(width_ + 2, std::vector<uint8_t>(height_ + 2, 0));
}

// Randomly populate cells in the board.
// We leave an empty cell of padding on all four sides to simplify neighbor
// calculation.
void Life::SetUpBoard(float density) {
  uint32_t cutoff = UINT32_MAX * density;

  ClearBoard();
  rounds_ = 0;
  for (size_t y = 0; y < height_; y++) {
    for (size_t x = 0; x < width_; x++) {
      if (esp_random() <= cutoff) {
        cells_[0][x + 1][y + 1] = 9;
      }
    }
  }

  kFadePalette[8] = kColors[random(sizeof(kColors) / sizeof(kColors[0]))];
}

void Life::DisplayBoard() {
  for (size_t y = 0; y < height_; y++) {
    for (size_t x = 0; x < width_; x++) {
      (*leds_)(x_ + x, y_ + y) =
          cells_[0][x + 1][y + 1] == 0
              ? CRGB::Black
              : kFadePalette[cells_[0][x + 1][y + 1] - 1];
    }
  }
}

// Run the life algorithm and replace the current board.
void Life::EvolveBoard() {
  cells_[3] = std::move(cells_[2]);
  cells_[2] = std::move(cells_[1]);
  cells_[1] = std::move(cells_[0]);
  ClearBoard();

  for (size_t y = 1; y <= height_; y++) {
    for (size_t x = 1; x <= width_; x++) {
      // Count the live neighbors
      int count = (cells_[1][x - 1][y - 1] == 9) + (cells_[1][x][y - 1] == 9) +
                  (cells_[1][x + 1][y - 1] == 9) + (cells_[1][x - 1][y] == 9) +
                  (cells_[1][x + 1][y] == 9) + (cells_[1][x - 1][y + 1] == 9) +
                  (cells_[1][x][y + 1] == 9) + (cells_[1][x + 1][y + 1] == 9);

      // Use the rules of life to update the cell
      if (cells_[1][x][y] == 9) {
        // Any live cell with two or three live neighbors survives.
        cells_[0][x][y] =
            (count == 2 || count == 3) ? 9 : max(0, cells_[1][x][y] - 1);
      } else {
        // Any dead cell with three live neighbors becomes a live cell.
        cells_[0][x][y] = (count == 3) ? 9 : max(0, cells_[1][x][y] - 1);
      }
    }
  }

  if (cells_[0] == cells_[1] || cells_[0] == cells_[2] ||
      cells_[0] == cells_[3] || ++rounds_ > kMaxRounds) {
    SetUpBoard(0.3);
  }
}

void Life::Animate() {
  EvolveBoard();
  DisplayBoard();
}

}  // namespace led_marquee