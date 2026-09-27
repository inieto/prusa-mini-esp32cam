#include "core/snapshot_service.hpp"

#include <gtest/gtest.h>

#include "fakes.hpp"

using core::ConnectState;
using core::Errc;
using core::Error;
using core::Millis;
using core::RebootReason;

namespace {

constexpr Millis kSec{1'000};
constexpr Millis kMin{60'000};

class SnapshotServiceTest : public ::testing::Test {
 protected:
  fakes::PoolStorage storage{3, 64};
  core::FramePool pool{storage.spans};
  fakes::ManualClock clock;
  fakes::FakeCamera camera{pool};
  fakes::FakeConnect connect;
  fakes::FakeNetwork network;
  fakes::FakeSystem system;
  fakes::CapturingLog log;

  core::SnapshotService make(core::SnapshotService::Options options = {}) {
    options.backoff.jitter_pct = 0;  // deterministic delays in assertions
    return core::SnapshotService({camera, connect, network, system, clock, log}, options,
                                 {.name = "cam", .firmware = "test"});
  }

  // Advance the clock one second at a time, ticking like the device task does.
  void run(core::SnapshotService& s, Millis duration) {
    for (Millis t{0}; t < duration; t += kSec) {
      s.tick();
      clock.advance(kSec);
    }
  }
};

TEST_F(SnapshotServiceTest, UploadsImmediatelyThenEveryInterval) {
  auto s = make({.default_interval = 30 * kSec});
  run(s, 61 * kSec);  // t = 0, 30, 60
  EXPECT_EQ(connect.uploads, 3u);
  EXPECT_EQ(s.status().state, ConnectState::Online);
  EXPECT_EQ(s.status().uploads_ok, 3u);
  EXPECT_TRUE(s.latest_frame());
}

TEST_F(SnapshotServiceTest, FollowsTriggerSchemeFromPrusaConnect) {
  connect.info_results.push_back(std::string(R"({"config":{"trigger_scheme":"TEN_SEC"}})"));
  auto s = make({.default_interval = 60 * kSec});
  run(s, 31 * kSec);  // 0, 10, 20, 30
  EXPECT_EQ(connect.uploads, 4u);
  EXPECT_EQ(s.status().interval, 10 * kSec);
}

TEST_F(SnapshotServiceTest, BackendErrorsBackOffButNeverTouchWifi) {
  for (int i = 0; i < 100; ++i) connect.upload_results.push_back(Error{Errc::HttpServer, 503});
  auto s = make({.default_interval = 10 * kSec});
  run(s, 60 * kMin);
  EXPECT_EQ(s.status().state, ConnectState::ServerError);
  EXPECT_EQ(system.reconnects, 0u) << "the server answered: WiFi is healthy (fixes B1)";
  EXPECT_TRUE(system.reboots.empty());
  EXPECT_LT(connect.uploads, 25u) << "backoff must throttle retries";
}

TEST_F(SnapshotServiceTest, TransportOutageEscalatesReconnectThenReboot) {
  for (int i = 0; i < 1000; ++i) connect.upload_results.push_back(Error{Errc::Transport});
  connect.info_results.push_back(std::string("{}"));  // info ok at t=0 sets the baseline
  for (int i = 0; i < 1000; ++i) connect.info_results.push_back(std::unexpected(Error{Errc::Transport}));
  auto s = make({.default_interval = 10 * kSec});

  run(s, 4 * kMin);
  EXPECT_EQ(system.reconnects, 0u);
  run(s, 2 * kMin);
  EXPECT_EQ(system.reconnects, 1u);
  EXPECT_TRUE(system.reboots.empty());
  run(s, 25 * kMin);
  ASSERT_FALSE(system.reboots.empty());
  EXPECT_EQ(system.reboots.front(), RebootReason::ConnectivityLost);
  EXPECT_EQ(s.status().state, ConnectState::Offline);
}

TEST_F(SnapshotServiceTest, RejectedTokenStopsHammeringAndReports) {
  for (int i = 0; i < 100; ++i) connect.upload_results.push_back(Error{Errc::Unauthorized, 401});
  auto s = make({.default_interval = 10 * kSec});
  run(s, 10 * kMin + kSec);
  EXPECT_EQ(s.status().state, ConnectState::TokenRejected);
  EXPECT_EQ(connect.uploads, 3u);  // t = 0, 5 min, 10 min
  EXPECT_TRUE(system.reboots.empty());
}

TEST_F(SnapshotServiceTest, WifiDownForLongRebootsEventually) {
  auto s = make({.default_interval = 10 * kSec});
  run(s, 2 * kSec);
  network.up = false;
  run(s, 29 * kMin);
  EXPECT_TRUE(system.reboots.empty());
  run(s, 2 * kMin);
  EXPECT_FALSE(system.reboots.empty());
}

TEST_F(SnapshotServiceTest, NotConfiguredNeverUploadsNorReboots) {
  connect.is_configured = false;
  auto s = make();
  run(s, 2 * 60 * kMin);
  EXPECT_EQ(connect.uploads, 0u);
  EXPECT_TRUE(connect.info_bodies.empty());
  EXPECT_TRUE(system.reboots.empty());
  EXPECT_EQ(s.status().state, ConnectState::NotConfigured);
}

TEST_F(SnapshotServiceTest, CaptureFailureRetriesSoonWithoutUploading) {
  camera.failures.push_back(Error{Errc::Timeout});
  auto s = make({.default_interval = 30 * kSec});
  run(s, 6 * kSec);  // fail at 0, retry at 5
  EXPECT_EQ(camera.captures, 2u);
  EXPECT_EQ(connect.uploads, 1u);
  EXPECT_EQ(s.status().capture_failures, 1u);
}

TEST_F(SnapshotServiceTest, ManualRequestCapturesWithoutWaitingForInterval) {
  auto s = make({.default_interval = 60 * kSec});
  run(s, 2 * kSec);
  s.request_snapshot();
  run(s, kSec);
  EXPECT_EQ(connect.uploads, 2u);
}

TEST_F(SnapshotServiceTest, InfoIsRefreshedHourly) {
  auto s = make();
  run(s, 2 * 60 * kMin + kSec);
  EXPECT_EQ(connect.info_bodies.size(), 3u);  // t = 0, 1 h, 2 h
}

}  // namespace
