// Escalating connectivity recovery (ADR-0004). Escalates only while the network is failing
// (no server reachable or WiFi down), measured from the last successful exchange. A server
// that answers — even with errors — proves the link is fine, so it never escalates.
#pragma once

#include "core/time.hpp"

namespace core {

enum class RecoveryAction : uint8_t { None, ReconnectWifi, Reboot };

class ConnectivityRecovery {
 public:
  struct Params {
    Millis reconnect_after{5 * 60'000};
    Millis reboot_after{30 * 60'000};
  };

  ConnectivityRecovery(Params params, Millis now);

  // Any completed exchange with a server counts, including HTTP error responses.
  void on_network_success(Millis now);
  // Server unreachable (DNS/TCP/TLS/timeout) or WiFi down.
  void on_network_failure() { failing_ = true; }
  RecoveryAction evaluate(Millis now);
  Millis since_last_success(Millis now) const { return now - last_success_; }

 private:
  Params params_;
  Millis last_success_;
  bool reconnect_issued_ = false;
  bool failing_ = false;
};

}  // namespace core
