// Pure rules for the SD card: which paths the API may touch, where timelapse frames go and which
// old log files to delete (F6: v1.1.2 never rotated its log). Host-tested.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/printer.hpp"
#include "core/time.hpp"

namespace core {

// Normalises a path relative to the card root ("timelapse/2026-11-09/x.jpg"). Rejects "..",
// backslashes, control characters, absolute paths and over-long names: the result can be
// appended to the mount point safely. "" and "/" mean the root.
std::optional<std::string> sanitize_relative_path(std::string_view path);

enum class TimelapseMode : uint8_t { Off, Always, WhilePrinting };

std::string_view to_string(TimelapseMode mode);
std::optional<TimelapseMode> timelapse_mode_from(std::string_view name);

struct StorageSettings {
  bool log_to_sd = true;
  TimelapseMode timelapse = TimelapseMode::Off;
  uint16_t timelapse_interval_s = 60;  // 10..3600; frames come from the snapshot schedule
  friend bool operator==(const StorageSettings&, const StorageSettings&) = default;
};
bool valid(const StorageSettings& settings);

struct TimelapseTarget {
  std::string folder;  // relative to the card root
  std::string file;    // "HHMMSS.jpg" (UTC): sorts by time, ffmpeg can glob it
};

class TimelapsePolicy {
 public:
  explicit TimelapsePolicy(StorageSettings settings = {}) : settings_(settings) {}
  void set(const StorageSettings& settings) { settings_ = settings; }

  // Decides whether a captured frame goes to the timelapse. Without wall time the frame is
  // skipped: file names would collide after a reboot.
  std::optional<TimelapseTarget> on_frame(Millis now, const PrinterJob& printer,
                                          std::optional<CivilTime> utc);

 private:
  StorageSettings settings_;
  std::optional<Millis> last_saved_;
  uint32_t job_id_ = 0;    // print the current folder belongs to
  std::string job_folder_;
};

// Given the log file names in the log folder ("YYYY-MM-DD.log"), returns those to delete so at
// most `keep` remain (oldest first).
std::vector<std::string> logs_to_delete(std::vector<std::string> names, size_t keep);

// Decodes a URL query value (%XX and '+'). Empty on malformed escapes or embedded NUL.
std::optional<std::string> url_decode(std::string_view encoded);

// Log file name for a date, or "undated.log" before NTP sync.
std::string log_file_name(std::optional<CivilTime> utc);

}  // namespace core
