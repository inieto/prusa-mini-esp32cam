// Diagnostics without a serial cable: RAM log ring buffer and reset history.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace device {

// Tees every ESP_LOG line into a ring buffer in PSRAM (visible in the web UI).
void install_log_buffer(size_t capacity);
std::string log_snapshot();

struct ResetRecord {
  std::string reason;
  uint32_t uptime_s;  // how long the previous run lasted (0 if unknown, e.g. power loss)
};

// Call once at boot: appends the previous reset (reason + last known uptime) to NVS history.
void record_boot(std::string_view reason);
std::vector<ResetRecord> reset_history();  // newest first, up to 8
// Called periodically so the uptime survives into the next boot (RTC memory).
void note_uptime(uint32_t seconds);

}  // namespace device
