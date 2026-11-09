// Home Assistant MQTT discovery and state payloads (ADR-0013). Pure: builds topics and JSON.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "core/printer.hpp"

namespace core::mqtt {

struct Identity {
  std::string node_id;    // stable, topic-safe: "prusacam_f8b3b7a84584"
  std::string name;       // shown in Home Assistant: the mDNS hostname
  std::string base;       // our topic prefix: "prusacam/<hostname>"
  std::string firmware;
  std::string discovery_prefix = "homeassistant";
};

struct Message {
  std::string topic;
  std::string payload;
  bool retain = true;
};

// Topics we publish to / listen on, all under identity.base.
std::string availability_topic(const Identity& id);
std::string state_topic(const Identity& id);
std::string image_topic(const Identity& id);
std::string light_command_topic(const Identity& id);
std::string snapshot_command_topic(const Identity& id);

// Retained discovery configs: camera image, light switch, snapshot button, diagnostic sensors.
std::vector<Message> discovery_messages(const Identity& id);

struct State {
  std::string connect;  // PrusaConnect state ("online", "offline", ...)
  bool light = false;
  int rssi = 0;
  uint32_t uploads_ok = 0;
  uint32_t uploads_failed = 0;
  uint32_t uptime_s = 0;
  PrinterJob printer;
  bool printer_known = false;  // PrusaLink configured
};
std::string state_payload(const State& state);

// Topic-safe node id from a MAC ("F8:B3:..." → "prusacam_f8b3b7a84584").
std::string node_id_from_mac(std::string_view mac);

}  // namespace core::mqtt
