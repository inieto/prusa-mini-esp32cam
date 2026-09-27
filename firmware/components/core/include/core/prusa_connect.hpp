// PrusaConnect camera API (connect.prusa3d.com/docs/cameras/openapi): pure encoding/decoding.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "core/camera_settings.hpp"
#include "core/error.hpp"
#include "core/time.hpp"

namespace core::connect {

inline constexpr std::string_view kDefaultHost = "connect.prusa3d.com";
inline constexpr std::string_view kSnapshotPath = "/c/snapshot";
inline constexpr std::string_view kInfoPath = "/c/info";
inline constexpr size_t kTokenLength = 20;

using Mac = std::array<uint8_t, 6>;

// Fingerprint exactly as Prusa-Firmware-ESP32-Cam v1.1.2 computes it. That firmware read the
// MAC before WiFi started, so it always embedded "00:00:00:00:00:00"; reproducing it keeps
// cameras already registered in PrusaConnect working after migration.
std::string legacy_fingerprint(const Mac& efuse_mac);

bool is_valid_token(std::string_view token);
bool is_valid_fingerprint(std::string_view fingerprint);  // 16..64 chars per the API

enum class TriggerScheme : uint8_t {
  TenSec, ThirtySec, SixtySec, TenMin, Manual, EachLayer, FifthLayer, Gcode,
};

std::optional<TriggerScheme> parse_trigger_scheme(std::string_view text);
// Interval to honour for a scheme; empty for schemes an OTHER camera cannot follow.
std::optional<Millis> interval_for(TriggerScheme scheme);

struct CameraIdentity {
  std::string name;
  std::string firmware;
  std::string_view manufacturer = "PrusaCam byClaude";
  std::string_view model = "ESP32-CAM AI Thinker (OV2640)";
};

struct NetworkSnapshot {
  bool connected = false;
  std::string ipv4;
  std::string ssid;
  std::string mac;  // "AA:BB:CC:DD:EE:FF"
  int8_t rssi = 0;
};

// Body for PUT /c/info. Never sends trigger_scheme: the user chooses it in PrusaConnect.
std::string build_info_json(const CameraIdentity& identity, const NetworkSnapshot& network,
                            Size resolution);

struct InfoResponse {
  std::optional<TriggerScheme> trigger_scheme;
  std::string name;
};

Result<InfoResponse> parse_info_response(std::string_view body);

}  // namespace core::connect
