#include "core/snapshot_service.hpp"

#include <cstdarg>
#include <cstdio>
#include <utility>

namespace core {
namespace {

__attribute__((format(printf, 3, 4))) void logf(Log& log, LogLevel level, const char* fmt, ...) {
  char buffer[192];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(buffer, sizeof buffer, fmt, args);
  va_end(args);
  log.write(level, buffer);
}

long long seconds(Millis m) { return static_cast<long long>(m.count() / 1000); }

}  // namespace

std::string_view to_string(ConnectState state) {
  switch (state) {
    case ConnectState::NotConfigured: return "not configured";
    case ConnectState::Starting: return "starting";
    case ConnectState::Online: return "online";
    case ConnectState::TokenRejected: return "token rejected";
    case ConnectState::Rejected: return "request rejected";
    case ConnectState::ServerError: return "server error";
    case ConnectState::Offline: return "offline";
  }
  return "unknown";
}

SnapshotService::SnapshotService(Deps deps, Options options, connect::CameraIdentity identity)
    : deps_(deps),
      options_(options),
      identity_(std::move(identity)),
      scheduler_(options.default_interval),
      backoff_(options.backoff, options.seed),
      recovery_(options.recovery, deps.clock.now()) {
  status_.interval = options.default_interval;
  status_.state =
      deps.connect.configured() ? ConnectState::Starting : ConnectState::NotConfigured;
}

void SnapshotService::tick() {
  const Millis now = deps_.clock.now();
  const bool configured = deps_.connect.configured();
  const bool online = deps_.network.connected();

  if (info_requested_.exchange(false)) next_info_at_.reset();
  if (snapshot_requested_.exchange(false)) scheduler_.trigger_now();

  if (!configured) {
    // Nothing to deliver, so nothing to recover: keep the baseline fresh.
    recovery_.on_network_success(now);
    set_state(ConnectState::NotConfigured);
  } else {
    if (status().state == ConnectState::NotConfigured) set_state(ConnectState::Starting);
    if (!online) recovery_.on_network_failure();
    run_recovery(now);
    if (online && (!next_info_at_ || now >= *next_info_at_)) sync_info(now);
  }

  if (scheduler_.due(now)) take_and_publish(now);

  std::lock_guard lock(mutex_);
  status_.next_capture_at = scheduler_.next_at();
  status_.interval = scheduler_.interval();
  status_.without_network_success = recovery_.since_last_success(now);
}

FrameRef SnapshotService::latest_frame() const {
  std::lock_guard lock(mutex_);
  return latest_;
}

ServiceStatus SnapshotService::status() const {
  std::lock_guard lock(mutex_);
  return status_;
}

void SnapshotService::run_recovery(Millis now) {
  switch (recovery_.evaluate(now)) {
    case RecoveryAction::None:
      break;
    case RecoveryAction::ReconnectWifi:
      logf(deps_.log, LogLevel::Warn, "No network success for %llds, reconnecting WiFi",
           seconds(recovery_.since_last_success(now)));
      deps_.system.reconnect_wifi();
      break;
    case RecoveryAction::Reboot:
      logf(deps_.log, LogLevel::Error, "No network success for %llds, rebooting",
           seconds(recovery_.since_last_success(now)));
      deps_.system.reboot(RebootReason::ConnectivityLost);
      break;
  }
}

void SnapshotService::sync_info(Millis now) {
  const std::string body = connect::build_info_json(
      identity_, deps_.network.current(), size_of(deps_.camera.settings().resolution));
  const Result<std::string> response = deps_.connect.put_info(body);

  if (!response) {
    const Error error = response.error();
    next_info_at_ = now + options_.info_retry;
    if (error.code == Errc::Transport || error.code == Errc::Timeout) {
      recovery_.on_network_failure();
    } else {
      recovery_.on_network_success(now);  // the server answered
    }
    if (error.code == Errc::Unauthorized) set_state(ConnectState::TokenRejected);
    logf(deps_.log, LogLevel::Warn, "Info sync failed: %s (%ld)",
         std::string(to_string(error.code)).c_str(), static_cast<long>(error.detail));
    return;
  }

  recovery_.on_network_success(now);
  next_info_at_ = now + options_.info_refresh;

  const Result<connect::InfoResponse> info = connect::parse_info_response(*response);
  if (!info || !info->trigger_scheme) return;
  const std::optional<Millis> interval = connect::interval_for(*info->trigger_scheme);
  if (interval && interval != scheduler_.interval()) {
    scheduler_.set_interval(interval, now);
    logf(deps_.log, LogLevel::Info, "PrusaConnect trigger interval: %llds", seconds(*interval));
  }
}

void SnapshotService::take_and_publish(Millis now) {
  Result<FrameRef> frame = deps_.camera.capture();
  if (!frame) {
    {
      std::lock_guard lock(mutex_);
      ++status_.capture_failures;
      status_.last_error = frame.error();
    }
    logf(deps_.log, LogLevel::Error, "Capture failed: %s (%ld)",
         std::string(to_string(frame.error().code)).c_str(),
         static_cast<long>(frame.error().detail));
    scheduler_.schedule_after(now, options_.capture_retry);
    return;
  }

  {
    std::lock_guard lock(mutex_);
    latest_ = *frame;
  }

  if (!deps_.connect.configured() || !deps_.network.connected()) {
    scheduler_.schedule_after(now);  // local capture only (e.g. requested from the web UI)
    return;
  }

  const Result<void> upload = deps_.connect.upload_snapshot(*frame);
  if (!upload) {
    record_failure(now, upload.error());
    return;
  }

  backoff_.reset();
  recovery_.on_network_success(now);
  {
    std::lock_guard lock(mutex_);
    ++status_.uploads_ok;
    status_.last_upload_ok_at = now;
    status_.last_error.reset();
  }
  set_state(ConnectState::Online);
  scheduler_.schedule_after(now);
}

void SnapshotService::record_failure(Millis now, Error error) {
  {
    std::lock_guard lock(mutex_);
    ++status_.uploads_failed;
    status_.last_error = error;
  }

  std::optional<Millis> retry;
  switch (error.code) {
    case Errc::Unauthorized:
      recovery_.on_network_success(now);
      set_state(ConnectState::TokenRejected);
      retry = options_.token_rejected_retry;
      break;
    case Errc::HttpClient:
      recovery_.on_network_success(now);
      set_state(ConnectState::Rejected);
      retry = backoff_.next();
      break;
    case Errc::HttpServer:
      recovery_.on_network_success(now);
      set_state(ConnectState::ServerError);
      retry = backoff_.next();
      break;
    default:
      recovery_.on_network_failure();
      set_state(ConnectState::Offline);
      retry = backoff_.next();
      break;
  }

  logf(deps_.log, LogLevel::Warn, "Upload failed: %s (%ld), retry in %llds",
       std::string(to_string(error.code)).c_str(), static_cast<long>(error.detail),
       seconds(*retry));
  scheduler_.schedule_after(now, retry);
}

void SnapshotService::set_state(ConnectState state) {
  std::lock_guard lock(mutex_);
  status_.state = state;
}

}  // namespace core
