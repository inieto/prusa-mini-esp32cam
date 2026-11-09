#include "device/mqtt_bridge.hpp"

#include <esp_log.h>
#include <esp_timer.h>

#include <cctype>

#include "device/camera.hpp"
#include "device/network.hpp"
#include "device/printer_monitor.hpp"

namespace device {
namespace {

constexpr const char* kTag = "mqtt";
constexpr uint32_t kStack = 4096;
constexpr TickType_t kStatePeriod = pdMS_TO_TICKS(30'000);

}  // namespace

bool MqttBridge::valid_uri(std::string_view uri) {
  if (!uri.starts_with("mqtt://") || uri.size() > 128) return false;
  for (char c : uri.substr(7)) {
    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == ':' || c == '_')) {
      return false;
    }
  }
  return uri.size() > 7;
}

void MqttBridge::start(const MqttSettings& settings, core::mqtt::Identity identity) {
  identity_ = std::move(identity);
  {
    std::lock_guard lock(mutex_);
    settings_ = settings;
  }
  xTaskCreatePinnedToCore(task_entry, "mqtt_bridge", kStack, this, 2, &task_, 1);
}

void MqttBridge::set_settings(const MqttSettings& settings) {
  {
    std::lock_guard lock(mutex_);
    settings_ = settings;
  }
  restart_pending_ = true;
  if (task_) xTaskNotifyGive(task_);
}

MqttSettings MqttBridge::settings() const {
  std::lock_guard lock(mutex_);
  return settings_;
}

void MqttBridge::on_frame(const core::FrameRef& frame) {
  if (!connected_) return;
  {
    std::lock_guard lock(mutex_);
    if (!settings_.publish_images) return;
    pending_ = frame;  // newest wins
  }
  if (task_) xTaskNotifyGive(task_);
}

void MqttBridge::task_entry(void* self) { static_cast<MqttBridge*>(self)->run(); }

void MqttBridge::run() {
  TickType_t last_state = 0;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, kStatePeriod);
    if (restart_pending_.exchange(false)) restart_client();
    if (!connected_) continue;

    if (announce_pending_.exchange(false)) announce();
    core::FrameRef frame;
    {
      std::lock_guard lock(mutex_);
      frame = std::move(pending_);
      pending_ = {};
    }
    if (frame) {
      const auto bytes = frame.bytes();
      // QoS 0 from this task: esp-mqtt streams the payload straight from the PSRAM frame, and a
      // slow broker only delays the next image, never the capture task.
      esp_mqtt_client_publish(client_, core::mqtt::image_topic(identity_).c_str(),
                              reinterpret_cast<const char*>(bytes.data()),
                              static_cast<int>(bytes.size()), 0, 0);
    }
    frame = {};
    const TickType_t now = xTaskGetTickCount();
    if (state_pending_.exchange(false) || now - last_state >= kStatePeriod) {
      last_state = now;
      publish_state();
    }
  }
}

void MqttBridge::restart_client() {
  if (client_) {
    esp_mqtt_client_stop(client_);
    esp_mqtt_client_destroy(client_);
    client_ = nullptr;
    connected_ = false;
  }
  const MqttSettings s = settings();
  if (s.uri.empty()) return;
  uri_ = s.uri;
  user_ = s.user;
  password_ = s.password;
  will_topic_ = core::mqtt::availability_topic(identity_);

  esp_mqtt_client_config_t config = {};
  config.broker.address.uri = uri_.c_str();
  if (!user_.empty()) config.credentials.username = user_.c_str();
  if (!password_.empty()) config.credentials.authentication.password = password_.c_str();
  config.credentials.client_id = identity_.node_id.c_str();
  config.session.last_will.topic = will_topic_.c_str();
  config.session.last_will.msg = "offline";
  config.session.last_will.qos = 1;
  config.session.last_will.retain = 1;
  config.session.keepalive = 60;
  config.network.reconnect_timeout_ms = 30'000;
  config.network.timeout_ms = 10'000;
  config.task.stack_size = 5120;
  client_ = esp_mqtt_client_init(&config);
  if (!client_) {
    ESP_LOGE(kTag, "client init failed");
    return;
  }
  esp_mqtt_client_register_event(client_, MQTT_EVENT_ANY, on_event, this);
  esp_mqtt_client_start(client_);
  ESP_LOGI(kTag, "connecting to %s", uri_.c_str());
}

void MqttBridge::on_event(void* self_ptr, esp_event_base_t, int32_t id, void* data) {
  auto* self = static_cast<MqttBridge*>(self_ptr);
  auto* ev = static_cast<esp_mqtt_event_handle_t>(data);
  switch (static_cast<esp_mqtt_event_id_t>(id)) {
    case MQTT_EVENT_CONNECTED:
      ESP_LOGI(kTag, "connected");
      self->connected_ = true;
      self->announce_pending_ = true;
      self->state_pending_ = true;
      if (self->task_) xTaskNotifyGive(self->task_);
      break;
    case MQTT_EVENT_DISCONNECTED:
      if (self->connected_) ESP_LOGW(kTag, "disconnected, retrying every 30 s");
      self->connected_ = false;
      break;
    case MQTT_EVENT_DATA: {
      const std::string_view topic(ev->topic, ev->topic_len);
      const std::string_view payload(ev->data, ev->data_len);
      if (topic == core::mqtt::light_command_topic(self->identity_)) {
        self->d_.camera.set_light(payload == "ON");  // thread-safe request
        self->state_pending_ = true;
        if (self->task_) xTaskNotifyGive(self->task_);
      } else if (topic == core::mqtt::snapshot_command_topic(self->identity_) && payload == "PRESS") {
        self->d_.service.request_snapshot();
      }
      break;
    }
    default:
      break;
  }
}

void MqttBridge::announce() {
  esp_mqtt_client_publish(client_, core::mqtt::availability_topic(identity_).c_str(), "online", 0,
                          1, 1);
  for (const core::mqtt::Message& m : core::mqtt::discovery_messages(identity_)) {
    esp_mqtt_client_publish(client_, m.topic.c_str(), m.payload.c_str(),
                            static_cast<int>(m.payload.size()), 1, m.retain);
  }
  esp_mqtt_client_subscribe(client_, core::mqtt::light_command_topic(identity_).c_str(), 0);
  esp_mqtt_client_subscribe(client_, core::mqtt::snapshot_command_topic(identity_).c_str(), 0);
}

void MqttBridge::publish_state() {
  const core::ServiceStatus s = d_.service.status();
  core::mqtt::State state;
  state.connect = s.uploads_paused ? "paused" : std::string(core::to_string(s.state));
  state.light = d_.camera.light();
  state.rssi = d_.network.current().rssi;
  state.uploads_ok = s.uploads_ok;
  state.uploads_failed = s.uploads_failed;
  state.uptime_s = static_cast<uint32_t>(esp_timer_get_time() / 1'000'000);
  state.printer_known = !d_.printer.settings().host.empty();
  state.printer = d_.printer.job();
  const std::string payload = core::mqtt::state_payload(state);
  esp_mqtt_client_publish(client_, core::mqtt::state_topic(identity_).c_str(), payload.c_str(),
                          static_cast<int>(payload.size()), 0, 1);
}

}  // namespace device
