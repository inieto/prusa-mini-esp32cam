#pragma once

#include "core/time.hpp"

namespace device {

struct EspClock final : core::Clock {
  core::Millis now() const override;
};

struct EspWallClock final : core::WallClock {
  std::optional<core::CivilTime> utc_now() const override;
};

// Seeds and secrets come from the ESP32 hardware RNG (RF thermal noise + ring oscillator).
// The core's xorshift is only used for backoff jitter, seeded from here.
uint32_t hardware_random();

}  // namespace device
