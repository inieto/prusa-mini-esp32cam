#include "device/diagnostics.hpp"

#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <nvs.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace device {
namespace {

// ---- log ring buffer ----
char* g_ring = nullptr;
size_t g_capacity = 0;
size_t g_head = 0;  // next write position
bool g_wrapped = false;
portMUX_TYPE g_ring_lock = portMUX_INITIALIZER_UNLOCKED;
vprintf_like_t g_previous = nullptr;

int tee_vprintf(const char* fmt, va_list args) {
  char line[256];
  va_list copy;
  va_copy(copy, args);
  int n = std::vsnprintf(line, sizeof line, fmt, copy);
  va_end(copy);
  if (n > 0 && g_ring) {
    n = std::min<int>(n, sizeof line - 1);
    portENTER_CRITICAL(&g_ring_lock);
    for (int i = 0; i < n; ++i) {
      g_ring[g_head] = line[i];
      if (++g_head == g_capacity) {
        g_head = 0;
        g_wrapped = true;
      }
    }
    portEXIT_CRITICAL(&g_ring_lock);
  }
  return g_previous ? g_previous(fmt, args) : std::vprintf(fmt, args);
}

// ---- reset history ----
constexpr uint32_t kUptimeMagic = 0x5EC0A11D;
RTC_NOINIT_ATTR uint32_t g_uptime_magic;
RTC_NOINIT_ATTR uint32_t g_uptime_s;

struct StoredRecord {
  char reason[40];
  uint32_t uptime_s;
};
constexpr size_t kHistory = 8;

}  // namespace

void install_log_buffer(size_t capacity) {
  g_ring = static_cast<char*>(heap_caps_calloc(1, capacity, MALLOC_CAP_SPIRAM));
  if (!g_ring) return;
  g_capacity = capacity;
  g_previous = esp_log_set_vprintf(tee_vprintf);
}

std::string log_snapshot() {
  std::string out;
  if (!g_ring) return out;
  out.reserve(g_capacity);
  // Copy in two steps to keep the critical section short and allocation-free.
  std::string tmp(g_capacity, '\0');
  size_t head;
  bool wrapped;
  portENTER_CRITICAL(&g_ring_lock);
  std::memcpy(tmp.data(), g_ring, g_capacity);
  head = g_head;
  wrapped = g_wrapped;
  portEXIT_CRITICAL(&g_ring_lock);
  if (wrapped) {
    out.append(tmp, head, std::string::npos);
    const size_t first_line = out.find('\n');  // drop the partial oldest line
    out.erase(0, first_line == std::string::npos ? 0 : first_line + 1);
  }
  out.append(tmp, 0, head);
  return out;
}

void note_uptime(uint32_t seconds) {
  g_uptime_s = seconds;
  g_uptime_magic = kUptimeMagic;
}

void record_boot(std::string_view reason) {
  const uint32_t previous_uptime = g_uptime_magic == kUptimeMagic ? g_uptime_s : 0;
  note_uptime(0);

  StoredRecord records[kHistory] = {};
  nvs_handle_t h;
  if (nvs_open("sys", NVS_READWRITE, &h) != ESP_OK) return;
  size_t len = sizeof records;
  if (nvs_get_blob(h, "resets", records, &len) != ESP_OK || len != sizeof records) {
    std::memset(records, 0, sizeof records);
  }
  std::memmove(&records[1], &records[0], sizeof(StoredRecord) * (kHistory - 1));
  std::snprintf(records[0].reason, sizeof records[0].reason, "%.*s",
                static_cast<int>(reason.size()), reason.data());
  records[0].uptime_s = previous_uptime;
  nvs_set_blob(h, "resets", records, sizeof records);
  nvs_commit(h);
  nvs_close(h);
}

std::vector<ResetRecord> reset_history() {
  std::vector<ResetRecord> out;
  StoredRecord records[kHistory] = {};
  nvs_handle_t h;
  if (nvs_open("sys", NVS_READONLY, &h) != ESP_OK) return out;
  size_t len = sizeof records;
  const bool ok = nvs_get_blob(h, "resets", records, &len) == ESP_OK && len == sizeof records;
  nvs_close(h);
  if (!ok) return out;
  for (const auto& r : records) {
    if (r.reason[0] == '\0') break;
    out.push_back({std::string(r.reason, strnlen(r.reason, sizeof r.reason)), r.uptime_s});
  }
  return out;
}

}  // namespace device
