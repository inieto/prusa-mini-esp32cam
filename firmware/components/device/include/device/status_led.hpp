// Drives the red status LED (GPIO33, active low) from a 125 ms timer, following the patterns of
// core::status_pattern. Can be switched off from the web UI (stored in NVS).
#pragma once

#include <esp_timer.h>

#include <atomic>

#include "core/snapshot_service.hpp"

namespace device {

class Network;

class StatusLed {
 public:
  StatusLed(core::SnapshotService& service, Network& network) : service_(service), network_(network) {}

  void start(bool enabled);
  void set_enabled(bool enabled);
  bool enabled() const { return enabled_; }

  static bool load_enabled();              // NVS "device/led", default on
  static void save_enabled(bool enabled);

 private:
  static void on_tick(void* self);

  core::SnapshotService& service_;
  Network& network_;
  esp_timer_handle_t timer_ = nullptr;
  std::atomic<bool> enabled_{true};
  uint32_t tick_ = 0;
};

}  // namespace device
