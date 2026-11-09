// Composition root: builds the adapters, wires them into the core and starts the tasks.
#include <esp_app_desc.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sdkconfig.h>

#include <array>
#include <span>

#include "core/snapshot_service.hpp"
#include "device/camera.hpp"
#include "device/clocks.hpp"
#include "device/auth.hpp"
#include "device/connect_client.hpp"
#include "device/diagnostics.hpp"
#include "device/factory_reset.hpp"
#include "device/firmware.hpp"
#include "device/live_stream.hpp"
#include "device/log.hpp"
#include "device/mqtt_bridge.hpp"
#include "device/network.hpp"
#include "device/printer_monitor.hpp"
#include "device/settings_store.hpp"
#include "device/storage.hpp"
#include "device/web_server.hpp"

namespace {

constexpr const char* kTag = "main";
// One slot is always the latest frame; the others cover an upload, the live stream and the
// timelapse/MQTT hand-overs. QXGA frames only fit three times next to the driver's buffers.
constexpr size_t kMaxFrameSlots = 4;
constexpr core::Millis kPreviewPeriod{200};  // live view: 5 fps at most
constexpr uint32_t kServiceStack = 10 * 1024;  // TLS handshake runs on this task
constexpr core::Millis kHealthyAfter{60'000};    // confirm OTA image after this uptime

// Long-lived objects: static storage, constructed once, never freed.
device::EspClock g_clock;
device::EspWallClock g_wall;
device::EspLog g_log;
device::SettingsStore g_store;
device::Network g_network;
device::EspSystem g_system{g_network};
device::PrusaConnectClient g_connect;
device::Auth g_auth;
device::Storage g_storage{g_clock, g_wall};
device::PrinterMonitor g_printer;

// Seeds the dev values from menuconfig into NVS the first time (bring-up convenience).
void seed_dev_settings(device::WifiCredentials& wifi, device::ConnectCredentials& connect) {
  if (wifi.ssid.empty() && sizeof(CONFIG_PRUSACAM_DEV_WIFI_SSID) > 1) {
    wifi.ssid = CONFIG_PRUSACAM_DEV_WIFI_SSID;
    wifi.password = CONFIG_PRUSACAM_DEV_WIFI_PASSWORD;
    g_store.save_wifi(wifi);
  }
  if (connect.token.empty() && sizeof(CONFIG_PRUSACAM_DEV_CONNECT_TOKEN) > 1) {
    connect.token = CONFIG_PRUSACAM_DEV_CONNECT_TOKEN;
    g_store.save_connect(connect);
  }
}

core::FramePool* make_frame_pool(core::Resolution largest) {
  // Sized for the sensor's largest resolution so resolution changes never need reallocation.
  const size_t slot = core::max_jpeg_size(largest);
  const size_t count = largest > core::Resolution::UXGA ? kMaxFrameSlots - 1 : kMaxFrameSlots;
  static std::array<std::span<uint8_t>, kMaxFrameSlots> buffers;
  for (size_t i = 0; i < count; ++i) {
    auto& b = buffers[i];
    auto* mem = static_cast<uint8_t*>(heap_caps_malloc(slot, MALLOC_CAP_SPIRAM));
    if (!mem) {
      ESP_LOGE(kTag, "PSRAM allocation for frame pool failed");
      return nullptr;
    }
    b = {mem, slot};
  }
  static core::FramePool pool{std::span<const std::span<uint8_t>>(buffers.data(), count)};
  ESP_LOGI(kTag, "frame pool: %u slots of %u KB", static_cast<unsigned>(count),
           static_cast<unsigned>(slot / 1024));
  return &pool;
}

struct CaptureTask {
  core::SnapshotService& service;
  device::LiveStream& stream;
};

// The only task that touches the camera (ADR-0003): the scheduled tick every second, plus
// flash-less preview frames while someone watches the live stream.
void service_task(void* arg) {
  auto& ctx = *static_cast<CaptureTask*>(arg);
  esp_task_wdt_add(nullptr);
  bool confirmed = false;
  core::Millis last_tick = g_clock.now() - core::Millis{1000};
  core::Millis last_preview{0};
  for (;;) {
    const core::Millis now = g_clock.now();
    if (now - last_tick >= core::Millis{1000}) {
      last_tick = now;
      ctx.service.tick();
    } else if (ctx.stream.watching() && now - last_preview >= kPreviewPeriod) {
      last_preview = now;
      ctx.service.capture_preview();
    }
    esp_task_wdt_reset();
    device::note_uptime(static_cast<uint32_t>(g_clock.now().count() / 1000));
    // A fresh update proves itself by running for a minute with the network up (or with no
    // WiFi configured at all). Until then any reset, crash or watchdog rolls back (ADR-0010).
    if (!confirmed && g_clock.now() >= kHealthyAfter &&
        (g_network.connected() || !g_network.station_configured())) {
      device::confirm_firmware();
      confirmed = true;
    }
    vTaskDelay(pdMS_TO_TICKS(ctx.stream.watching() ? 20 : 100));
  }
}

}  // namespace

extern "C" void app_main() {
  device::install_log_buffer(16 * 1024);
  const esp_app_desc_t* app = esp_app_get_description();
  const std::string_view reset_reason = device::last_reset_reason();
  ESP_LOGI(kTag, "PrusaCam byClaude %s, previous reset: %.*s", app->version,
           static_cast<int>(reset_reason.size()), reset_reason.data());

  if (auto r = device::SettingsStore::init_flash(); !r) ESP_LOGE(kTag, "NVS init failed");
  device::record_boot(reset_reason);
  if (const auto fw = device::firmware_status(); fw.rolled_back_version) {
    ESP_LOGW(kTag, "update %s failed its health check and was rolled back",
             fw.rolled_back_version->c_str());
  }
  if (const auto dump = device::coredump_info()) {
    ESP_LOGW(kTag, "core dump stored (%u B, task %s, pc 0x%08lx): download it from System",
             static_cast<unsigned>(dump->size), dump->task.c_str(),
             static_cast<unsigned long>(dump->pc));
  }
  device::check_factory_reset_pin(g_system);
  g_auth.load();

  device::WifiCredentials wifi = g_store.load_wifi();
  device::ConnectCredentials connect = g_store.load_connect();
  seed_dev_settings(wifi, connect);

  if (auto r = g_network.start(wifi, device::setup_ap_password()); !r) {
    ESP_LOGE(kTag, "network start failed (%ld)", static_cast<long>(r.error().detail));
  }

  // Keep the fingerprint of the v1.1.2 registration so PrusaConnect still knows this camera.
  if (connect.fingerprint.empty()) {
    connect.fingerprint = core::connect::legacy_fingerprint(g_network.efuse_mac());
    g_store.save_connect(connect);
  }
  g_connect.set_credentials(connect);

  static device::EspCamera camera{g_wall, g_clock};
  if (auto r = camera.init(g_store.load_camera()); !r) {
    ESP_LOGE(kTag, "camera init failed; snapshots disabled until reboot");
  }
  core::FramePool* pool = make_frame_pool(camera.max_resolution());
  if (!pool) return;
  camera.attach_pool(*pool);

  static core::SnapshotService service{
      {camera, g_connect, g_network, g_system, g_clock, g_log},
      {.seed = device::hardware_random()},
      {.name = wifi.hostname, .firmware = app->version}};

  const device::PrinterSettings printer = g_store.load_printer();
  g_printer.start(printer);
  service.set_printer(&g_printer);
  service.set_upload_only_while_printing(printer.upload_only_while_printing);
  g_storage.start(g_store.load_storage(), &g_printer);
  service.add_observer(g_storage);

  static device::MqttBridge mqtt{{service, camera, g_network, g_printer}};
  mqtt.start(g_store.load_mqtt(),
             {.node_id = core::mqtt::node_id_from_mac(g_network.mac_string()),
              .name = wifi.hostname,
              .base = "prusacam/" + wifi.hostname,
              .firmware = app->version});
  service.add_observer(mqtt);

  static device::LiveStream stream{service};
  stream.start();
  static CaptureTask capture_task{service, stream};
  xTaskCreatePinnedToCore(service_task, "snapshot", kServiceStack, &capture_task, 5, nullptr, 1);

  const core::Result<core::WebBundle> bundle = core::WebBundle::parse(device::embedded_web_bundle());
  if (!bundle) ESP_LOGE(kTag, "embedded web UI is corrupt");
  static device::WebServer web{{service, camera, g_connect, g_store, g_storage, g_printer, mqtt, stream,
                                g_network, g_system, g_auth, g_clock,
                                bundle.value_or(core::WebBundle{}), app->version}};
  if (auto r = web.start(); !r) ESP_LOGE(kTag, "HTTP server failed to start");
  if (g_auth.setup_required()) {
    ESP_LOGW(kTag, "No web user yet: open the UI within 15 minutes to create one");
  }
  ESP_LOGI(kTag, "started; internal free %u B, PSRAM free %u B",
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
}
