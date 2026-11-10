// Typed persistence on NVS. Camera settings are a versioned blob: bump kCameraSchema when
// CameraSettings changes layout (older blobs are then ignored and defaults are used).
#pragma once

#include <optional>
#include <string>

#include "core/camera_settings.hpp"
#include "core/storage_policy.hpp"
#include "device/connect_client.hpp"
#include "device/mqtt_bridge.hpp"
#include "device/printer_monitor.hpp"

namespace device {

struct WifiCredentials {
  std::string ssid;
  std::string password;
  std::string hostname = "prusacam";
  // Max transmit power. Lower peaks draw less current: with a strong signal (-50 dBm) 13 dBm is
  // plenty, and it avoids drops on marginal 5 V supplies ("missing ACKs", reason 34).
  uint8_t tx_power_dbm = 20;
};
bool valid_tx_power(int dbm);  // 8..20

class SettingsStore {
 public:
  static core::Result<void> init_flash();  // nvs_flash_init with erase-on-version-mismatch

  WifiCredentials load_wifi() const;
  core::Result<void> save_wifi(const WifiCredentials& wifi);

  ConnectCredentials load_connect() const;
  core::Result<void> save_connect(const ConnectCredentials& connect);

  core::CameraSettings load_camera() const;
  core::Result<void> save_camera(const core::CameraSettings& camera);

  MqttSettings load_mqtt() const;
  core::Result<void> save_mqtt(const MqttSettings& mqtt);

  PrinterSettings load_printer() const;
  core::Result<void> save_printer(const PrinterSettings& printer);

  core::StorageSettings load_storage() const;
  core::Result<void> save_storage(const core::StorageSettings& storage);

 private:
  // 2: framing appended, 3: live view quality, 4: sharpness/denoise/white balance mode
  static constexpr uint8_t kCameraSchema = 4;
  static constexpr uint8_t kStorageSchema = 1;
};

}  // namespace device
