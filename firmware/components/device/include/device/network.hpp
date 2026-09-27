// WiFi station with automatic reconnection, setup access point, mDNS and SNTP.
#pragma once

#include <esp_event.h>

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "core/ports.hpp"
#include "device/settings_store.hpp"

namespace device {

struct ScanResult {
  std::string ssid;
  int8_t rssi;
  uint8_t channel;
  std::string_view auth;
};

class Network final : public core::NetworkInfo {
 public:
  // Starts STA if credentials exist, otherwise the setup AP "PrusaCam-XXXX" on 192.168.4.1.
  core::Result<void> start(const WifiCredentials& wifi, const std::string& ap_password);

  bool connected() const override { return connected_; }
  core::connect::NetworkSnapshot current() const override;
  bool setup_ap_active() const { return ap_active_; }
  std::string mac_string() const;
  core::connect::Mac efuse_mac() const;

  void reconnect();  // drop and re-associate (recovery ladder step 2)
  std::vector<ScanResult> scan();  // blocking, ~2-3 s

 private:
  static void on_event(void* arg, esp_event_base_t base, int32_t id, void* data);
  void start_services(const std::string& hostname);

  std::atomic<bool> connected_{false};
  std::atomic<bool> ap_active_{false};
  bool have_sta_ = false;
  mutable std::mutex mutex_;
  std::string ip_;
  std::string ssid_;
};

class EspSystem final : public core::SystemControl {
 public:
  explicit EspSystem(Network& network) : network_(network) {}
  void reconnect_wifi() override { network_.reconnect(); }
  void reboot(core::RebootReason reason) override;

 private:
  Network& network_;
};

// Reason for the previous reset, combining esp_reset_reason() with our own requested reboots.
std::string_view last_reset_reason();

// Per-device secret for the setup AP, generated once from the hardware RNG and kept in NVS.
std::string setup_ap_password();

}  // namespace device
