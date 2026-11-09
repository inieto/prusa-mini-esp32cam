// MQTT bridge to Home Assistant (ADR-0013): discovery, state every 30 s, the latest snapshot as
// a camera image, and commands (light, take snapshot). Plain MQTT on the LAN only: a second TLS
// session would break the one-TLS-session rule of ADR-0006.
#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mqtt_client.h>

#include <atomic>
#include <mutex>
#include <string>

#include "core/mqtt_discovery.hpp"
#include "core/ports.hpp"
#include "core/snapshot_service.hpp"

namespace device {

class EspCamera;
class Network;
class PrinterMonitor;

struct MqttSettings {
  std::string uri;   // "mqtt://192.168.0.10:1883"; empty = disabled
  std::string user;
  std::string password;
  bool publish_images = true;
};

class MqttBridge final : public core::FrameObserver {
 public:
  struct Deps {
    core::SnapshotService& service;
    EspCamera& camera;
    Network& network;
    PrinterMonitor& printer;
  };

  explicit MqttBridge(Deps deps) : d_(deps) {}

  void start(const MqttSettings& settings, core::mqtt::Identity identity);
  void set_settings(const MqttSettings& settings);  // thread-safe; reconnects
  MqttSettings settings() const;
  bool connected() const { return connected_; }

  void on_frame(const core::FrameRef& frame) override;  // capture task: hand over, never block

  static bool valid_uri(std::string_view uri);

 private:
  static void task_entry(void* self);
  static void on_event(void* self, esp_event_base_t base, int32_t id, void* data);
  void run();
  void restart_client();
  void announce();
  void publish_state();

  Deps d_;
  core::mqtt::Identity identity_;
  TaskHandle_t task_ = nullptr;
  esp_mqtt_client_handle_t client_ = nullptr;
  std::atomic<bool> connected_{false};
  std::atomic<bool> announce_pending_{false};
  std::atomic<bool> state_pending_{false};
  std::atomic<bool> restart_pending_{true};

  mutable std::mutex mutex_;  // guards settings_ and pending_
  MqttSettings settings_;
  core::FrameRef pending_;

  // Owned by the bridge task (the client keeps pointers into them while it lives).
  std::string uri_, user_, password_, will_topic_;
};

}  // namespace device
