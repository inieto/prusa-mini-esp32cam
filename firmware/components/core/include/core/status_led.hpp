// What the red status LED (GPIO33) shows. When everything is fine it blinks once a minute
// (alive, but unobtrusive: it sits next to the printer, often in a dark room). Pure, host-tested.
#pragma once

#include <cstdint>

#include "core/snapshot_service.hpp"

namespace core {

enum class LedPattern : uint8_t {
  Heartbeat,    // all good (or nothing configured yet): one 500 ms blink per minute
  SlowBlink,    // no WiFi: connecting, or the setup AP is up
  DoubleBlink,  // WiFi fine, but PrusaConnect uploads are failing
};

LedPattern status_pattern(bool wifi_connected, bool setup_ap, ConnectState connect);

// LED state for a pattern at a given 125 ms tick. Blink patterns repeat every 2 s (16 ticks), the
// heartbeat every minute (480 ticks).
bool led_on(LedPattern pattern, uint32_t tick);

}  // namespace core
