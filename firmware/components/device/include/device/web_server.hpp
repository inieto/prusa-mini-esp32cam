// HTTP server: embedded UI + REST API /api/v1 (see tools/mock_server.py for the contract).
// Handlers never call drivers directly: they ask the owning actor (ADR-0003).
#pragma once

#include <esp_http_server.h>

#include <string>

#include "core/snapshot_service.hpp"
#include "core/web_bundle.hpp"
#include "device/auth.hpp"
#include "device/camera.hpp"
#include "device/connect_client.hpp"
#include "device/network.hpp"
#include "device/settings_store.hpp"

namespace device {

// The UI bundle linked into the firmware image (see tools/pack_web.py).
std::span<const uint8_t> embedded_web_bundle();

class WebServer {
 public:
  struct Deps {
    core::SnapshotService& service;
    Ov2640Camera& camera;
    PrusaConnectClient& connect;
    SettingsStore& store;
    Network& network;
    core::SystemControl& system;
    Auth& auth;
    const core::Clock& clock;
    core::WebBundle bundle;
    std::string version;
  };

  explicit WebServer(Deps deps) : d_(std::move(deps)) {}
  core::Result<void> start();

 private:
  // Route handlers (httpd runs them one at a time in its own task).
  static esp_err_t on_static(httpd_req_t* req);
  static esp_err_t on_api(httpd_req_t* req);

  esp_err_t handle_api(httpd_req_t* req, std::string_view route);
  esp_err_t get_status(httpd_req_t* req);
  esp_err_t get_snapshot(httpd_req_t* req);
  esp_err_t login_or_setup(httpd_req_t* req, bool setup);
  esp_err_t camera_config(httpd_req_t* req, bool update);
  esp_err_t connect_config(httpd_req_t* req, bool update);
  esp_err_t network_config(httpd_req_t* req, bool update);
  esp_err_t wifi_scan(httpd_req_t* req);

  bool is_authenticated(httpd_req_t* req) const;

  Deps d_;
  httpd_handle_t server_ = nullptr;
};

}  // namespace device
