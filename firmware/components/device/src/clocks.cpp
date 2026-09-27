#include "device/clocks.hpp"

#include <esp_random.h>
#include <esp_timer.h>

#include <ctime>

namespace device {

core::Millis EspClock::now() const { return core::Millis{esp_timer_get_time() / 1000}; }

std::optional<core::CivilTime> EspWallClock::utc_now() const {
  const std::time_t now = std::time(nullptr);
  std::tm t{};
  gmtime_r(&now, &t);
  if (t.tm_year + 1900 < 2024) return std::nullopt;  // SNTP has not synchronised yet
  return core::CivilTime{static_cast<uint16_t>(t.tm_year + 1900),
                         static_cast<uint8_t>(t.tm_mon + 1), static_cast<uint8_t>(t.tm_mday),
                         static_cast<uint8_t>(t.tm_hour), static_cast<uint8_t>(t.tm_min),
                         static_cast<uint8_t>(t.tm_sec)};
}

uint32_t hardware_random() { return esp_random(); }

}  // namespace device
