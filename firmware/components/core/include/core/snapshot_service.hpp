// Use case: keep PrusaConnect fed with fresh snapshots, recovering from failures without
// hurting the local network. Runs in a single task; the public "request" methods are safe to
// call from other tasks (e.g. the HTTP server).
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>

#include "core/backoff.hpp"
#include "core/ports.hpp"
#include "core/recovery.hpp"
#include "core/scheduler.hpp"

namespace core {

enum class ConnectState : uint8_t {
  NotConfigured,  // no token/fingerprint yet
  Starting,       // configured, first exchange pending
  Online,         // last exchange succeeded
  TokenRejected,  // 401/403: user must fix the token
  Rejected,       // other 4xx
  ServerError,    // 5xx
  Offline,        // server unreachable (DNS/TCP/TLS/timeout)
};

std::string_view to_string(ConnectState state);

struct ServiceStatus {
  ConnectState state = ConnectState::NotConfigured;
  uint32_t uploads_ok = 0;
  uint32_t uploads_failed = 0;
  uint32_t capture_failures = 0;
  std::optional<Millis> last_upload_ok_at;
  std::optional<Error> last_error;
  std::optional<Millis> interval;  // empty = manual
  std::optional<Millis> next_capture_at;
  Millis without_network_success{0};
};

class SnapshotService {
 public:
  struct Deps {
    CameraPort& camera;
    ConnectApi& connect;
    NetworkInfo& network;
    SystemControl& system;
    const Clock& clock;
    Log& log;
  };

  struct Options {
    std::optional<Millis> default_interval{30'000};
    Millis info_refresh{60 * 60'000};
    Millis info_retry{60'000};
    Millis capture_retry{5'000};
    Millis token_rejected_retry{5 * 60'000};
    Backoff::Params backoff{};
    ConnectivityRecovery::Params recovery{};
    uint32_t seed = 1;
  };

  SnapshotService(Deps deps, Options options, connect::CameraIdentity identity);

  // Drive from the owning task roughly once per second.
  void tick();

  // Thread-safe requests.
  void request_snapshot() { snapshot_requested_ = true; }
  void request_info_sync() { info_requested_ = true; }
  FrameRef latest_frame() const;
  ServiceStatus status() const;

 private:
  void run_recovery(Millis now);
  void sync_info(Millis now);
  void take_and_publish(Millis now);
  void record_failure(Millis now, Error error);
  void set_state(ConnectState state);

  Deps deps_;
  Options options_;
  connect::CameraIdentity identity_;

  SnapshotScheduler scheduler_;
  Backoff backoff_;
  ConnectivityRecovery recovery_;
  std::optional<Millis> next_info_at_;  // empty = sync as soon as possible

  std::atomic<bool> snapshot_requested_{false};
  std::atomic<bool> info_requested_{false};

  mutable std::mutex mutex_;  // guards status_ and latest_
  ServiceStatus status_;
  FrameRef latest_;
};

}  // namespace core
