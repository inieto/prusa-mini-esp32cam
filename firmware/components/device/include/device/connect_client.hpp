// HTTPS client for the PrusaConnect camera API with one persistent TLS session (ADR-0006).
#pragma once

#include <esp_http_client.h>

#include <mutex>
#include <string>

#include "core/ports.hpp"

namespace device {

struct ConnectCredentials {
  std::string host{core::connect::kDefaultHost};
  std::string token;
  std::string fingerprint;
};

class PrusaConnectClient final : public core::ConnectApi {
 public:
  PrusaConnectClient() = default;
  ~PrusaConnectClient() override;
  PrusaConnectClient(const PrusaConnectClient&) = delete;
  PrusaConnectClient& operator=(const PrusaConnectClient&) = delete;

  void set_credentials(ConnectCredentials credentials);  // thread-safe
  ConnectCredentials credentials() const;

  core::Result<void> upload_snapshot(const core::FrameRef& frame) override;
  core::Result<std::string> put_info(std::string_view json) override;
  bool configured() const override;

 private:
  core::Result<std::string> put(std::string_view path, std::string_view content_type,
                                std::span<const uint8_t> body);
  void reset_connection();

  mutable std::mutex mutex_;
  ConnectCredentials credentials_;
  std::string connected_host_;
  esp_http_client_handle_t client_ = nullptr;
};

}  // namespace device
