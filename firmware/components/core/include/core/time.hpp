// Time abstractions. Monotonic time drives all policies; wall time is only for labels.
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

namespace core {

using Millis = std::chrono::milliseconds;  // monotonic, since boot

struct Clock {
  virtual ~Clock() = default;
  virtual Millis now() const = 0;
};

struct CivilTime {
  uint16_t year;
  uint8_t month, day, hour, minute, second;  // month 1-12
};

struct WallClock {
  virtual ~WallClock() = default;
  // Empty until NTP has synchronised at least once.
  virtual std::optional<CivilTime> utc_now() const = 0;
};

}  // namespace core
