#include "core/recovery.hpp"

namespace core {

ConnectivityRecovery::ConnectivityRecovery(Params params, Millis now)
    : params_(params), last_success_(now) {}

void ConnectivityRecovery::on_network_success(Millis now) {
  last_success_ = now;
  reconnect_issued_ = false;
  failing_ = false;
}

RecoveryAction ConnectivityRecovery::evaluate(Millis now) {
  if (!failing_) return RecoveryAction::None;
  const Millis elapsed = now - last_success_;
  if (elapsed >= params_.reboot_after) return RecoveryAction::Reboot;
  if (elapsed >= params_.reconnect_after && !reconnect_issued_) {
    reconnect_issued_ = true;
    return RecoveryAction::ReconnectWifi;
  }
  return RecoveryAction::None;
}

}  // namespace core
