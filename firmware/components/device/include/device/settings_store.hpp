// Typed persistence on NVS. Camera settings are a versioned blob: bump kCameraSchema when
// CameraSettings changes layout (older blobs are then ignored and defaults are used).
#pragma once

#include <optional>
#include <string>

#include "core/camera_settings.hpp"
#include "device/connect_client.hpp"

namespace device {

struct WifiCredentials {
  std::string ssid;
  std::string password;
  std::string hostname = "prusacam";
};

class SettingsStore {
 public:
  static core::Result<void> init_flash();  // nvs_flash_init with erase-on-version-mismatch

  WifiCredentials load_wifi() const;
  core::Result<void> save_wifi(const WifiCredentials& wifi);

  ConnectCredentials load_connect() const;
  core::Result<void> save_connect(const ConnectCredentials& connect);

  core::CameraSettings load_camera() const;
  core::Result<void> save_camera(const core::CameraSettings& camera);

 private:
  static constexpr uint8_t kCameraSchema = 1;
};

}  // namespace device
