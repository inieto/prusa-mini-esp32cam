#include "core/status_led.hpp"

namespace core {

LedPattern status_pattern(bool wifi_connected, bool setup_ap, ConnectState connect) {
  if (!wifi_connected || setup_ap) return LedPattern::SlowBlink;
  switch (connect) {
    case ConnectState::Offline:
    case ConnectState::ServerError:
    case ConnectState::Rejected:
    case ConnectState::TokenRejected:
      return LedPattern::DoubleBlink;
    case ConnectState::NotConfigured:
    case ConnectState::Starting:
    case ConnectState::Online:
      return LedPattern::Off;
  }
  return LedPattern::Off;
}

bool led_on(LedPattern pattern, uint32_t tick) {
  const uint32_t t = tick % 16;
  switch (pattern) {
    case LedPattern::Off: return false;
    case LedPattern::SlowBlink: return t < 8;                 // 1 s on, 1 s off
    case LedPattern::DoubleBlink: return t == 0 || t == 2;    // blip-blip, then 1.6 s dark
  }
  return false;
}

}  // namespace core
