// Interfaces the core needs from the outside world (hexagonal "driven" ports).
#pragma once

#include <string>
#include <string_view>

#include "core/camera_settings.hpp"
#include "core/error.hpp"
#include "core/frame_pool.hpp"
#include "core/prusa_connect.hpp"

namespace core {

struct CameraPort {
  virtual ~CameraPort() = default;
  virtual Result<FrameRef> capture() = 0;
  virtual Result<void> apply(const CameraSettings& settings) = 0;
  virtual CameraSettings settings() const = 0;
};

struct ConnectApi {
  virtual ~ConnectApi() = default;
  // Errors must be classified: Transport when no HTTP response arrived, otherwise the
  // Errc matching the status code (see classify_http_status) with the status as detail.
  virtual Result<void> upload_snapshot(const FrameRef& frame) = 0;
  virtual Result<std::string> put_info(std::string_view json) = 0;
  virtual bool configured() const = 0;  // token and fingerprint present
};

struct NetworkInfo {
  virtual ~NetworkInfo() = default;
  virtual bool connected() const = 0;  // cheap: STA associated and has an IP
  virtual connect::NetworkSnapshot current() const = 0;
};

enum class RebootReason : uint8_t { Requested, ConnectivityLost, OtaApplied, ConfigReset };

struct SystemControl {
  virtual ~SystemControl() = default;
  virtual void reconnect_wifi() = 0;
  virtual void reboot(RebootReason reason) = 0;
};

enum class LogLevel : uint8_t { Error, Warn, Info, Debug };

struct Log {
  virtual ~Log() = default;
  virtual void write(LogLevel level, std::string_view message) = 0;
};

// Maps an HTTP status to the error taxonomy used by retry policies (ADR-0004).
Errc classify_http_status(int status);

}  // namespace core
