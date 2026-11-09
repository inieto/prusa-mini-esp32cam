#include "core/mqtt_discovery.hpp"

#include <cJSON.h>

#include <cctype>
#include <memory>

namespace core::mqtt {
namespace {

struct JsonDeleter {
  void operator()(cJSON* j) const { cJSON_Delete(j); }
};
using JsonPtr = std::unique_ptr<cJSON, JsonDeleter>;

std::string print(cJSON* json) {
  char* text = cJSON_PrintUnformatted(json);
  std::string out = text ? text : "{}";
  cJSON_free(text);
  return out;
}

// Fields every entity shares: availability, device block, unique id.
JsonPtr entity(const Identity& id, std::string_view key, std::string_view name) {
  JsonPtr j(cJSON_CreateObject());
  cJSON_AddStringToObject(j.get(), "name", std::string(name).c_str());
  cJSON_AddStringToObject(j.get(), "unique_id", (id.node_id + "_" + std::string(key)).c_str());
  cJSON_AddStringToObject(j.get(), "availability_topic", availability_topic(id).c_str());
  cJSON* dev = cJSON_AddObjectToObject(j.get(), "device");
  cJSON* ids = cJSON_AddArrayToObject(dev, "identifiers");
  cJSON_AddItemToArray(ids, cJSON_CreateString(id.node_id.c_str()));
  cJSON_AddStringToObject(dev, "name", id.name.c_str());
  cJSON_AddStringToObject(dev, "manufacturer", "AI Thinker");
  cJSON_AddStringToObject(dev, "model", "ESP32-CAM (PrusaCam byClaude)");
  cJSON_AddStringToObject(dev, "sw_version", id.firmware.c_str());
  return j;
}

Message config(const Identity& id, std::string_view component, std::string_view key, cJSON* body) {
  return {id.discovery_prefix + "/" + std::string(component) + "/" + id.node_id + "/" +
              std::string(key) + "/config",
          print(body), true};
}

Message sensor(const Identity& id, std::string_view key, std::string_view name,
               std::string_view template_, std::string_view unit = {},
               std::string_view device_class = {}, bool diagnostic = true) {
  JsonPtr j = entity(id, key, name);
  cJSON_AddStringToObject(j.get(), "state_topic", state_topic(id).c_str());
  cJSON_AddStringToObject(j.get(), "value_template", std::string(template_).c_str());
  if (!unit.empty()) cJSON_AddStringToObject(j.get(), "unit_of_measurement", std::string(unit).c_str());
  if (!device_class.empty()) {
    cJSON_AddStringToObject(j.get(), "device_class", std::string(device_class).c_str());
  }
  if (diagnostic) cJSON_AddStringToObject(j.get(), "entity_category", "diagnostic");
  return config(id, "sensor", key, j.get());
}

}  // namespace

std::string availability_topic(const Identity& id) { return id.base + "/availability"; }
std::string state_topic(const Identity& id) { return id.base + "/state"; }
std::string image_topic(const Identity& id) { return id.base + "/image"; }
std::string light_command_topic(const Identity& id) { return id.base + "/light/set"; }
std::string snapshot_command_topic(const Identity& id) { return id.base + "/snapshot/set"; }

std::vector<Message> discovery_messages(const Identity& id) {
  std::vector<Message> out;

  JsonPtr cam = entity(id, "camera", "Snapshot");
  cJSON_AddStringToObject(cam.get(), "topic", image_topic(id).c_str());
  out.push_back(config(id, "camera", "camera", cam.get()));

  JsonPtr light = entity(id, "light", "Light");
  cJSON_AddStringToObject(light.get(), "command_topic", light_command_topic(id).c_str());
  cJSON_AddStringToObject(light.get(), "state_topic", state_topic(id).c_str());
  cJSON_AddStringToObject(light.get(), "value_template", "{{ 'ON' if value_json.light else 'OFF' }}");
  cJSON_AddStringToObject(light.get(), "icon", "mdi:flashlight");
  out.push_back(config(id, "switch", "light", light.get()));

  JsonPtr snap = entity(id, "snapshot", "Take snapshot");
  cJSON_AddStringToObject(snap.get(), "command_topic", snapshot_command_topic(id).c_str());
  cJSON_AddStringToObject(snap.get(), "payload_press", "PRESS");
  out.push_back(config(id, "button", "snapshot", snap.get()));

  out.push_back(sensor(id, "connect", "PrusaConnect", "{{ value_json.connect }}", {}, {}, false));
  out.push_back(sensor(id, "printer", "Printer", "{{ value_json.printer.state }}", {}, {}, false));
  out.push_back(sensor(id, "progress", "Print progress", "{{ value_json.printer.progress }}", "%",
                       {}, false));
  out.push_back(sensor(id, "rssi", "WiFi signal", "{{ value_json.rssi }}", "dBm", "signal_strength"));
  out.push_back(sensor(id, "uploads_ok", "Uploads OK", "{{ value_json.uploads_ok }}"));
  out.push_back(sensor(id, "uploads_failed", "Uploads failed", "{{ value_json.uploads_failed }}"));
  out.push_back(sensor(id, "uptime", "Uptime", "{{ value_json.uptime_s }}", "s", "duration"));
  return out;
}

std::string state_payload(const State& s) {
  JsonPtr j(cJSON_CreateObject());
  cJSON_AddStringToObject(j.get(), "connect", s.connect.c_str());
  cJSON_AddBoolToObject(j.get(), "light", s.light);
  cJSON_AddNumberToObject(j.get(), "rssi", s.rssi);
  cJSON_AddNumberToObject(j.get(), "uploads_ok", s.uploads_ok);
  cJSON_AddNumberToObject(j.get(), "uploads_failed", s.uploads_failed);
  cJSON_AddNumberToObject(j.get(), "uptime_s", s.uptime_s);
  cJSON* p = cJSON_AddObjectToObject(j.get(), "printer");
  cJSON_AddStringToObject(p, "state",
                          s.printer_known ? std::string(to_string(s.printer.phase)).c_str() : "n/a");
  cJSON_AddNumberToObject(p, "progress", static_cast<int>(s.printer.progress));
  cJSON_AddStringToObject(p, "file", s.printer.file.c_str());
  cJSON_AddNumberToObject(p, "time_left_s", s.printer.time_left_s);
  return print(j.get());
}

std::string node_id_from_mac(std::string_view mac) {
  std::string out = "prusacam_";
  for (char c : mac) {
    if (std::isxdigit(static_cast<unsigned char>(c))) {
      out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
  }
  return out;
}

}  // namespace core::mqtt
