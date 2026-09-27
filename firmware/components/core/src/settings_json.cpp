#include "core/settings_json.hpp"

#include <cJSON.h>

#include <array>
#include <memory>

namespace core {
namespace {

struct JsonDeleter {
  void operator()(cJSON* j) const { cJSON_Delete(j); }
};
using JsonPtr = std::unique_ptr<cJSON, JsonDeleter>;

constexpr std::array<std::string_view, 7> kResolutionNames = {"qvga", "cif",  "vga", "svga",
                                                              "xga",  "sxga", "uxga"};

int rotation_degrees(Rotation r) { return static_cast<int>(r) * 90; }

// Visitor over every field: keeps JSON names, types and struct members in one place.
template <class Visitor>
bool visit_fields(CameraSettings& s, Visitor&& v) {
  return v.integer("jpeg_quality", s.jpeg_quality) && v.integer("brightness", s.brightness) &&
         v.integer("contrast", s.contrast) && v.integer("saturation", s.saturation) &&
         v.boolean("auto_exposure", s.auto_exposure) && v.boolean("aec_dsp", s.aec_dsp) &&
         v.integer("ae_level", s.ae_level) && v.integer("manual_exposure", s.manual_exposure) &&
         v.boolean("auto_gain", s.auto_gain) && v.integer("gain_ceiling", s.gain_ceiling) &&
         v.integer("manual_gain", s.manual_gain) &&
         v.boolean("auto_white_balance", s.auto_white_balance) &&
         v.boolean("hmirror", s.hmirror) && v.boolean("vflip", s.vflip) &&
         v.boolean("lens_correction", s.lens_correction) &&
         v.boolean("flash_on_capture", s.flash_on_capture) &&
         v.integer("flash_lead_ms", s.flash_lead_ms) &&
         v.integer("flash_duty_pct", s.flash_duty_pct);
}

struct Writer {
  cJSON* root;
  template <class T>
  bool integer(const char* key, T& value) {
    cJSON_AddNumberToObject(root, key, static_cast<double>(value));
    return true;
  }
  bool boolean(const char* key, bool& value) {
    cJSON_AddBoolToObject(root, key, value);
    return true;
  }
};

struct Reader {
  const cJSON* root;
  int consumed = 0;
  template <class T>
  bool integer(const char* key, T& value) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!item) return true;
    ++consumed;
    if (!cJSON_IsNumber(item)) return false;
    const double d = item->valuedouble;
    if (d != static_cast<double>(static_cast<long>(d))) return false;  // integers only
    const long n = static_cast<long>(d);
    if (n < -32768 || n > 65535) return false;
    value = static_cast<T>(n);
    return static_cast<long>(value) == n;  // reject values the member type cannot hold
  }
  bool boolean(const char* key, bool& value) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!item) return true;
    ++consumed;
    if (!cJSON_IsBool(item)) return false;
    value = cJSON_IsTrue(item);
    return true;
  }
};

}  // namespace

std::string camera_settings_to_json(const CameraSettings& settings) {
  CameraSettings copy = settings;
  JsonPtr root(cJSON_CreateObject());
  cJSON_AddStringToObject(root.get(),
                          "resolution",
                          std::string(kResolutionNames[static_cast<size_t>(copy.resolution)]).c_str());
  cJSON_AddNumberToObject(root.get(), "rotation", rotation_degrees(copy.rotation));
  visit_fields(copy, Writer{root.get()});
  char* text = cJSON_PrintUnformatted(root.get());
  std::string out = text ? text : "{}";
  cJSON_free(text);
  return out;
}

Result<CameraSettings> merge_camera_settings(const CameraSettings& settings,
                                             std::string_view json) {
  JsonPtr root(cJSON_ParseWithLength(json.data(), json.size()));
  if (!root || !cJSON_IsObject(root.get())) return fail(Errc::InvalidArgument);

  CameraSettings next = settings;
  int consumed = 0;

  if (const cJSON* r = cJSON_GetObjectItemCaseSensitive(root.get(), "resolution")) {
    ++consumed;
    if (!cJSON_IsString(r)) return fail(Errc::InvalidArgument);
    bool found = false;
    for (size_t i = 0; i < kResolutionNames.size(); ++i) {
      if (kResolutionNames[i] == r->valuestring) {
        next.resolution = static_cast<Resolution>(i);
        found = true;
      }
    }
    if (!found) return fail(Errc::OutOfRange);
  }
  if (const cJSON* r = cJSON_GetObjectItemCaseSensitive(root.get(), "rotation")) {
    ++consumed;
    if (!cJSON_IsNumber(r)) return fail(Errc::InvalidArgument);
    const int deg = r->valueint;
    if (deg % 90 != 0 || deg < 0 || deg > 270) return fail(Errc::OutOfRange);
    next.rotation = static_cast<Rotation>(deg / 90);
  }

  Reader reader{root.get()};
  if (!visit_fields(next, reader)) return fail(Errc::InvalidArgument);
  consumed += reader.consumed;
  if (consumed != cJSON_GetArraySize(root.get())) return fail(Errc::InvalidArgument);  // unknown key
  if (auto v = validate(next); !v) return std::unexpected(v.error());
  return next;
}

}  // namespace core
