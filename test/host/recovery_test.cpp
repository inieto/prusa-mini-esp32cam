#include "core/recovery.hpp"

#include <gtest/gtest.h>

using core::ConnectivityRecovery;
using core::Millis;
using core::RecoveryAction;

namespace {
constexpr Millis kMin{60'000};
}

TEST(Recovery, EscalatesReconnectOnceThenReboot) {
  ConnectivityRecovery r({.reconnect_after = 5 * kMin, .reboot_after = 30 * kMin}, Millis{0});
  r.on_network_failure();
  EXPECT_EQ(r.evaluate(4 * kMin), RecoveryAction::None);
  EXPECT_EQ(r.evaluate(5 * kMin), RecoveryAction::ReconnectWifi);
  EXPECT_EQ(r.evaluate(6 * kMin), RecoveryAction::None);  // only once per outage
  EXPECT_EQ(r.evaluate(29 * kMin), RecoveryAction::None);
  EXPECT_EQ(r.evaluate(30 * kMin), RecoveryAction::Reboot);
}

TEST(Recovery, SuccessResetsTheLadder) {
  ConnectivityRecovery r({.reconnect_after = 5 * kMin, .reboot_after = 30 * kMin}, Millis{0});
  r.on_network_failure();
  EXPECT_EQ(r.evaluate(5 * kMin), RecoveryAction::ReconnectWifi);
  r.on_network_success(6 * kMin);
  EXPECT_EQ(r.evaluate(10 * kMin), RecoveryAction::None);
  r.on_network_failure();
  EXPECT_EQ(r.evaluate(11 * kMin), RecoveryAction::ReconnectWifi);  // new outage, new attempt
  EXPECT_EQ(r.evaluate(35 * kMin), RecoveryAction::None);
  EXPECT_EQ(r.evaluate(36 * kMin), RecoveryAction::Reboot);
}

TEST(Recovery, IdleTimeWithoutFailuresNeverEscalates) {
  // Long backoff after server errors: time passes without attempts, but nothing failed.
  ConnectivityRecovery r({.reconnect_after = 5 * kMin, .reboot_after = 30 * kMin}, Millis{0});
  EXPECT_EQ(r.evaluate(60 * kMin), RecoveryAction::None);
}
