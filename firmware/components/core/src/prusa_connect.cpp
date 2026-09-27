#include "core/prusa_connect.hpp"

#include <cJSON.h>

#include <array>
#include <cctype>
#include <memory>

#include "core/base64.hpp"

namespace core::connect {
namespace {

struct JsonDeleter {
  void operator()(cJSON* json) const { cJSON_Delete(json); }
};
using JsonPtr = std::unique_ptr<cJSON, JsonDeleter>;

struct SchemeName {
  TriggerScheme scheme;
  std::string_view name;
};

constexpr std::array<SchemeName, 8> kSchemes = {{
    {TriggerScheme::TenSec, "TEN_SEC"},
    {TriggerScheme::ThirtySec, "THIRTY_SEC"},
    {TriggerScheme::SixtySec, "SIXTY_SEC"},
    {TriggerScheme::TenMin, "TEN_MIN"},
    {TriggerScheme::Manual, "MANUAL"},
    {TriggerScheme::EachLayer, "EACH_LAYER"},
    {TriggerScheme::FifthLayer, "FIFTH_LAYER"},
    {TriggerScheme::Gcode, "GCODE"},
}};

std::string_view string_field(const cJSON* object, const char* key) {
  const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
  return cJSON_IsString(item) && item->valuestring ? item->valuestring : std::string_view{};
}

}  // namespace

std::string legacy_fingerprint(const Mac& efuse_mac) {
  std::string id;
  for (uint8_t byte : efuse_mac) id += std::to_string(byte);
  id += " 00:00:00:00:00:00";
  return base64_encode({reinterpret_cast<const uint8_t*>(id.data()), id.size()});
}

bool is_valid_token(std::string_view token) {
  if (token.size() != kTokenLength) return false;
  for (char c : token) {
    if (!std::isalnum(static_cast<unsigned char>(c))) return false;
  }
  return true;
}

bool is_valid_fingerprint(std::string_view fingerprint) {
  if (fingerprint.size() < 16 || fingerprint.size() > 64) return false;
  for (char c : fingerprint) {
    if (c <= ' ' || c > '~') return false;  // printable, no spaces: it goes in a header
  }
  return true;
}

std::optional<TriggerScheme> parse_trigger_scheme(std::string_view text) {
  for (const auto& [scheme, name] : kSchemes) {
    if (name == text) return scheme;
  }
  return std::nullopt;
}

std::optional<Millis> interval_for(TriggerScheme scheme) {
  switch (scheme) {
    case TriggerScheme::TenSec: return Millis{10'000};
    case TriggerScheme::ThirtySec: return Millis{30'000};
    case TriggerScheme::SixtySec: return Millis{60'000};
    case TriggerScheme::TenMin: return Millis{600'000};
    default: return std::nullopt;  // layer/gcode/manual are only for printer-attached cameras
  }
}

std::string build_info_json(const CameraIdentity& identity, const NetworkSnapshot& network,
                            Size resolution) {
  JsonPtr root(cJSON_CreateObject());
  cJSON* config = cJSON_AddObjectToObject(root.get(), "config");
  cJSON_AddStringToObject(config, "name", identity.name.c_str());
  cJSON_AddStringToObject(config, "firmware", identity.firmware.c_str());
  cJSON_AddStringToObject(config, "manufacturer", std::string(identity.manufacturer).c_str());
  cJSON_AddStringToObject(config, "model", std::string(identity.model).c_str());

  cJSON* res = cJSON_AddObjectToObject(config, "resolution");
  cJSON_AddNumberToObject(res, "width", resolution.width);
  cJSON_AddNumberToObject(res, "height", resolution.height);

  cJSON* net = cJSON_AddObjectToObject(config, "network_info");
  cJSON_AddStringToObject(net, "wifi_mac", network.mac.c_str());
  cJSON_AddStringToObject(net, "wifi_ipv4", network.ipv4.c_str());
  cJSON_AddStringToObject(net, "wifi_ssid", network.ssid.c_str());

  cJSON* capabilities = cJSON_AddArrayToObject(root.get(), "capabilities");
  cJSON_AddItemToArray(capabilities, cJSON_CreateString("trigger_scheme"));

  char* text = cJSON_PrintUnformatted(root.get());
  std::string out = text ? text : "";
  cJSON_free(text);
  return out;
}

Result<InfoResponse> parse_info_response(std::string_view body) {
  JsonPtr root(cJSON_ParseWithLength(body.data(), body.size()));
  if (!root || !cJSON_IsObject(root.get())) return fail(Errc::InvalidArgument);

  InfoResponse response;
  response.name = std::string(string_field(root.get(), "name"));
  if (const cJSON* config = cJSON_GetObjectItemCaseSensitive(root.get(), "config")) {
    response.trigger_scheme = parse_trigger_scheme(string_field(config, "trigger_scheme"));
    if (response.name.empty()) response.name = std::string(string_field(config, "name"));
  }
  return response;
}

}  // namespace core::connect
