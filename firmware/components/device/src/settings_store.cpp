#include "device/settings_store.hpp"

#include <esp_log.h>
#include <nvs.h>
#include <nvs_flash.h>

#include <cstring>
#include <type_traits>

namespace device {
namespace {

constexpr const char* kTag = "settings";

class Nvs {
 public:
  Nvs(const char* ns, nvs_open_mode_t mode) { ok_ = nvs_open(ns, mode, &handle_) == ESP_OK; }
  ~Nvs() {
    if (ok_) nvs_close(handle_);
  }
  Nvs(const Nvs&) = delete;
  Nvs& operator=(const Nvs&) = delete;

  std::string str(const char* key, std::string fallback = {}) const {
    size_t len = 0;
    if (!ok_ || nvs_get_str(handle_, key, nullptr, &len) != ESP_OK || len == 0) return fallback;
    std::string out(len, '\0');
    nvs_get_str(handle_, key, out.data(), &len);
    out.resize(len - 1);  // drop NUL
    return out;
  }
  esp_err_t set(const char* key, const std::string& value) {
    return ok_ ? nvs_set_str(handle_, key, value.c_str()) : ESP_ERR_NVS_NOT_INITIALIZED;
  }
  bool blob(const char* key, void* out, size_t size) const {
    size_t len = size;
    return ok_ && nvs_get_blob(handle_, key, out, &len) == ESP_OK && len == size;
  }
  esp_err_t set_blob(const char* key, const void* data, size_t size) {
    return ok_ ? nvs_set_blob(handle_, key, data, size) : ESP_ERR_NVS_NOT_INITIALIZED;
  }
  core::Result<void> commit() {
    const esp_err_t err = ok_ ? nvs_commit(handle_) : ESP_ERR_NVS_NOT_INITIALIZED;
    if (err != ESP_OK) return core::fail(core::Errc::Io, err);
    return {};
  }

 private:
  nvs_handle_t handle_{};
  bool ok_ = false;
};

static_assert(std::is_trivially_copyable_v<core::CameraSettings>, "stored as raw NVS blob");

struct CameraBlob {
  uint8_t schema;
  core::CameraSettings settings;
};

}  // namespace

core::Result<void> SettingsStore::init_flash() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(kTag, "NVS layout changed, erasing");
    nvs_flash_erase();
    err = nvs_flash_init();
  }
  if (err != ESP_OK) return core::fail(core::Errc::Io, err);
  return {};
}

WifiCredentials SettingsStore::load_wifi() const {
  Nvs nvs("wifi", NVS_READONLY);
  WifiCredentials w;
  w.ssid = nvs.str("ssid");
  w.password = nvs.str("pass");
  w.hostname = nvs.str("host", w.hostname);
  return w;
}

core::Result<void> SettingsStore::save_wifi(const WifiCredentials& w) {
  Nvs nvs("wifi", NVS_READWRITE);
  nvs.set("ssid", w.ssid);
  nvs.set("pass", w.password);
  nvs.set("host", w.hostname);
  return nvs.commit();
}

ConnectCredentials SettingsStore::load_connect() const {
  Nvs nvs("connect", NVS_READONLY);
  ConnectCredentials c;
  c.host = nvs.str("host", c.host);
  c.token = nvs.str("token");
  c.fingerprint = nvs.str("fprint");
  return c;
}

core::Result<void> SettingsStore::save_connect(const ConnectCredentials& c) {
  Nvs nvs("connect", NVS_READWRITE);
  nvs.set("host", c.host);
  nvs.set("token", c.token);
  nvs.set("fprint", c.fingerprint);
  return nvs.commit();
}

core::CameraSettings SettingsStore::load_camera() const {
  Nvs nvs("camera", NVS_READONLY);
  CameraBlob blob{};
  if (nvs.blob("settings", &blob, sizeof blob) && blob.schema == kCameraSchema &&
      core::validate(blob.settings)) {
    return blob.settings;
  }
  return {};
}

core::Result<void> SettingsStore::save_camera(const core::CameraSettings& camera) {
  if (auto v = core::validate(camera); !v) return v;
  CameraBlob blob{};
  blob.schema = kCameraSchema;
  blob.settings = camera;
  Nvs nvs("camera", NVS_READWRITE);
  nvs.set_blob("settings", &blob, sizeof blob);
  return nvs.commit();
}

}  // namespace device
