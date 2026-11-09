// What the camera knows about the printer (from PrusaLink, ADR-0012). Pure values.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace core {

enum class PrinterPhase : uint8_t { Unknown, Idle, Busy, Printing, Paused, Finished, Error };

std::string_view to_string(PrinterPhase phase);

struct PrinterJob {
  PrinterPhase phase = PrinterPhase::Unknown;
  uint32_t job_id = 0;      // 0 = no job
  std::string file;         // display name of the file being printed
  float progress = 0;       // 0..100
  uint32_t time_left_s = 0;
};

// Latest known printer state; thread-safe, never blocks (served from a cache).
struct PrinterInfo {
  virtual ~PrinterInfo() = default;
  virtual PrinterJob job() const = 0;
};

}  // namespace core
