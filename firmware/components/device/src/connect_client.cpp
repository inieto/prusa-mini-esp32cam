#include "device/connect_client.hpp"

#include <esp_crt_bundle.h>
#include <esp_log.h>

#include <algorithm>

namespace device {
namespace {

constexpr const char* kTag = "connect";
constexpr int kTimeoutMs = 15'000;
constexpr size_t kChunk = 4096;
constexpr size_t kMaxResponse = 4096;

}  // namespace

PrusaConnectClient::~PrusaConnectClient() { reset_connection(); }

void PrusaConnectClient::set_credentials(ConnectCredentials credentials) {
  std::lock_guard lock(mutex_);
  credentials_ = std::move(credentials);
}

ConnectCredentials PrusaConnectClient::credentials() const {
  std::lock_guard lock(mutex_);
  return credentials_;
}

bool PrusaConnectClient::configured() const {
  std::lock_guard lock(mutex_);
  return core::connect::is_valid_token(credentials_.token) &&
         core::connect::is_valid_fingerprint(credentials_.fingerprint) &&
         !credentials_.host.empty();
}

void PrusaConnectClient::reset_connection() {
  if (client_) {
    esp_http_client_cleanup(client_);
    client_ = nullptr;
  }
  connected_host_.clear();
}

core::Result<void> PrusaConnectClient::upload_snapshot(const core::FrameRef& frame) {
  auto r = put(core::connect::kSnapshotPath, "image/jpg", frame.bytes());
  if (!r) return std::unexpected(r.error());
  return {};
}

core::Result<std::string> PrusaConnectClient::put_info(std::string_view json) {
  return put(core::connect::kInfoPath, "application/json",
             {reinterpret_cast<const uint8_t*>(json.data()), json.size()});
}

// Called only from the SnapshotService task; the mutex only protects credentials.
core::Result<std::string> PrusaConnectClient::put(std::string_view path,
                                                  std::string_view content_type,
                                                  std::span<const uint8_t> body) {
  const ConnectCredentials creds = credentials();
  const std::string url = "https://" + creds.host + std::string(path);

  if (!client_ || connected_host_ != creds.host) {
    reset_connection();
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.method = HTTP_METHOD_PUT;
    config.timeout_ms = kTimeoutMs;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.keep_alive_enable = true;
    config.buffer_size = 2048;
    config.buffer_size_tx = 1024;
    client_ = esp_http_client_init(&config);
    if (!client_) return core::fail(core::Errc::NoMemory);
    connected_host_ = creds.host;
  } else {
    esp_http_client_set_url(client_, url.c_str());
    esp_http_client_set_method(client_, HTTP_METHOD_PUT);
  }

  esp_http_client_set_header(client_, "Content-Type", std::string(content_type).c_str());
  esp_http_client_set_header(client_, "Token", creds.token.c_str());
  esp_http_client_set_header(client_, "Fingerprint", creds.fingerprint.c_str());
  esp_http_client_set_header(client_, "User-Agent", "PrusaCam-byClaude");

  esp_err_t err = esp_http_client_open(client_, static_cast<int>(body.size()));
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "open %s: %s", url.c_str(), esp_err_to_name(err));
    reset_connection();
    return core::fail(core::Errc::Transport, err);
  }

  for (size_t sent = 0; sent < body.size();) {
    const size_t n = std::min(kChunk, body.size() - sent);
    const int w = esp_http_client_write(client_, reinterpret_cast<const char*>(body.data() + sent),
                                        static_cast<int>(n));
    if (w <= 0) {
      reset_connection();
      return core::fail(core::Errc::Transport, w);
    }
    sent += static_cast<size_t>(w);
  }

  if (esp_http_client_fetch_headers(client_) < 0) {
    reset_connection();
    return core::fail(core::Errc::Timeout);
  }
  const int status = esp_http_client_get_status_code(client_);

  std::string response;
  char buffer[256];
  for (;;) {
    const int n = esp_http_client_read(client_, buffer, sizeof buffer);
    if (n <= 0) break;
    if (response.size() < kMaxResponse) response.append(buffer, static_cast<size_t>(n));
  }
  if (!esp_http_client_is_complete_data_received(client_)) reset_connection();

  if (status < 200 || status > 299) {
    ESP_LOGW(kTag, "PUT %.*s -> %d", static_cast<int>(path.size()), path.data(), status);
    return core::fail(core::classify_http_status(status), status);
  }
  return response;
}

}  // namespace device
