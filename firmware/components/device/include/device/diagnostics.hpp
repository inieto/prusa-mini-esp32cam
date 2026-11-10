// Diagnostics without a serial cable: RAM log ring buffer and reset history.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace device {

// Tees every ESP_LOG line into a ring buffer in PSRAM (visible in the web UI) and into a small
// ring in RTC memory that survives crashes, watchdogs and software reboots (not power loss).
void install_log_buffer(size_t capacity);
// The RTC log tail left by the previous run (empty after a power-on or if it was damaged).
std::string_view previous_log();
std::string log_snapshot();
// Log text written since `cursor` (a running byte count, start at 0); advances the cursor.
// If more than the ring holds was written meanwhile, the oldest part is lost.
std::string log_read_since(uint64_t& cursor);

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
