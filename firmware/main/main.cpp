// Composition root: builds the adapters, wires them into the core and starts the tasks.
#include <esp_app_desc.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
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
#include "device/log.hpp"
#include "device/network.hpp"
#include "device/settings_store.hpp"
#include "device/web_server.hpp"

namespace {

constexpr const char* kTag = "main";
constexpr size_t kFrameSlots = 3;
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

core::FramePool* make_frame_pool() {
  // Worst case (UXGA, best quality) so resolution changes never need reallocation.
  const size_t slot = core::max_jpeg_size(core::Resolution::UXGA, 10);
  static std::array<std::span<uint8_t>, kFrameSlots> buffers;
  for (auto& b : buffers) {
    auto* mem = static_cast<uint8_t*>(heap_caps_malloc(slot, MALLOC_CAP_SPIRAM));
    if (!mem) {
      ESP_LOGE(kTag, "PSRAM allocation for frame pool failed");
      return nullptr;
    }
    b = {mem, slot};
  }
  static core::FramePool pool{buffers};
  return &pool;
}

void service_task(void* arg) {
  auto& service = *static_cast<core::SnapshotService*>(arg);
  esp_task_wdt_add(nullptr);
  bool confirmed = false;
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    service.tick();
    esp_task_wdt_reset();
    device::note_uptime(static_cast<uint32_t>(g_clock.now().count() / 1000));
    if (!confirmed && g_clock.now() >= kHealthyAfter) {
      esp_ota_mark_app_valid_cancel_rollback();  // no-op when not booting a fresh OTA image
      confirmed = true;
    }
    vTaskDelayUntil(&last, pdMS_TO_TICKS(1000));
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

  core::FramePool* pool = make_frame_pool();
  if (!pool) return;
  static device::Ov2640Camera camera{*pool, g_wall, g_clock};
  if (auto r = camera.init(g_store.load_camera()); !r) {
    ESP_LOGE(kTag, "camera init failed; snapshots disabled until reboot");
  }

  static core::SnapshotService service{
      {camera, g_connect, g_network, g_system, g_clock, g_log},
      {.seed = device::hardware_random()},
      {.name = wifi.hostname, .firmware = app->version}};

  xTaskCreatePinnedToCore(service_task, "snapshot", kServiceStack, &service, 5, nullptr, 1);

  const core::Result<core::WebBundle> bundle = core::WebBundle::parse(device::embedded_web_bundle());
  if (!bundle) ESP_LOGE(kTag, "embedded web UI is corrupt");
  static device::WebServer web{{service, camera, g_connect, g_store, g_network, g_system, g_auth,
                                g_clock, bundle.value_or(core::WebBundle{}), app->version}};
  if (auto r = web.start(); !r) ESP_LOGE(kTag, "HTTP server failed to start");
  if (g_auth.setup_required()) {
    ESP_LOGW(kTag, "No web user yet: open the UI within 15 minutes to create one");
  }
  ESP_LOGI(kTag, "started; internal free %u B, PSRAM free %u B",
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
}
