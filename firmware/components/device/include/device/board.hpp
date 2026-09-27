// AI Thinker ESP32-CAM pin map and fixed capabilities.
#pragma once

#include <driver/gpio.h>

namespace device::board {

inline constexpr int kCamPwdn = 32;
inline constexpr int kCamReset = -1;
inline constexpr int kCamXclk = 0;
inline constexpr int kCamSda = 26;
inline constexpr int kCamScl = 27;
inline constexpr int kCamD7 = 35, kCamD6 = 34, kCamD5 = 39, kCamD4 = 36;
inline constexpr int kCamD3 = 21, kCamD2 = 19, kCamD1 = 18, kCamD0 = 5;
inline constexpr int kCamVsync = 25, kCamHref = 23, kCamPclk = 22;
inline constexpr int kCamXclkHz = 20'000'000;

inline constexpr gpio_num_t kFlashLed = GPIO_NUM_4;   // white LED, shares SD DATA1 (SD runs 1-bit)
inline constexpr gpio_num_t kStatusLed = GPIO_NUM_33;  // red LED, active low

}  // namespace device::board
