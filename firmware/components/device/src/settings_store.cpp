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
  // Reads a blob of at most `size` bytes; returns its length (0 if absent or larger).
  size_t blob_prefix(const char* key, void* out, size_t size) const {
    size_t len = 0;
    if (!ok_ || nvs_get_blob(handle_, key, nullptr, &len) != ESP_OK || len > size) return 0;
    return nvs_get_blob(handle_, key, out, &len) == ESP_OK ? len : 0;
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
static_assert(std::is_trivially_copyable_v<core::StorageSettings>, "stored as raw NVS blob");

template <class Settings>
struct Blob {
  uint8_t schema;
  Settings settings;
};
using CameraBlob = Blob<core::CameraSettings>;
using StorageBlob = Blob<core::StorageSettings>;

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
  const size_t len = nvs.blob_prefix("settings", &blob, sizeof blob);
  if (len == 0) return {};
  if (blob.schema == 1) {
    // Schema 1 predates framing; the fields were appended, so its bytes are a prefix. Whatever
    // lies beyond it was zero-filled above: give the new fields their defaults.
    const core::CameraSettings defaults;
    if (blob.settings.zoom_pct == 0) {
      blob.settings.zoom_pct = defaults.zoom_pct;
      blob.settings.pan_x = defaults.pan_x;
      blob.settings.pan_y = defaults.pan_y;
    }
  } else if (blob.schema != kCameraSchema || len != sizeof blob) {
    return {};
  }
  return core::validate(blob.settings) ? blob.settings : core::CameraSettings{};
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

MqttSettings SettingsStore::load_mqtt() const {
  Nvs nvs("mqtt", NVS_READONLY);
  MqttSettings m;
  m.uri = nvs.str("uri");
  m.user = nvs.str("user");
  m.password = nvs.str("pass");
  m.publish_images = nvs.str("images", "1") == "1";
  return m;
}

core::Result<void> SettingsStore::save_mqtt(const MqttSettings& m) {
  Nvs nvs("mqtt", NVS_READWRITE);
  nvs.set("uri", m.uri);
  nvs.set("user", m.user);
  nvs.set("pass", m.password);
  nvs.set("images", m.publish_images ? "1" : "0");
  return nvs.commit();
}

PrinterSettings SettingsStore::load_printer() const {
  Nvs nvs("link", NVS_READONLY);
  PrinterSettings p;
  p.host = nvs.str("host");
  p.user = nvs.str("user", p.user);
  p.password = nvs.str("pass");
  p.upload_only_while_printing = nvs.str("only_print") == "1";
  return p;
}

core::Result<void> SettingsStore::save_printer(const PrinterSettings& p) {
  Nvs nvs("link", NVS_READWRITE);
  nvs.set("host", p.host);
  nvs.set("user", p.user);
  nvs.set("pass", p.password);
  nvs.set("only_print", p.upload_only_while_printing ? "1" : "0");
  return nvs.commit();
}

core::StorageSettings SettingsStore::load_storage() const {
  Nvs nvs("storage", NVS_READONLY);
  StorageBlob blob{};
  if (nvs.blob("settings", &blob, sizeof blob) && blob.schema == kStorageSchema &&
      core::valid(blob.settings)) {
    return blob.settings;
  }
  return {};
}

core::Result<void> SettingsStore::save_storage(const core::StorageSettings& storage) {
  if (!core::valid(storage)) return core::fail(core::Errc::OutOfRange);
  StorageBlob blob{};
  blob.schema = kStorageSchema;
  blob.settings = storage;
  Nvs nvs("storage", NVS_READWRITE);
  nvs.set_blob("settings", &blob, sizeof blob);
  return nvs.commit();
}

}  // namespace device
