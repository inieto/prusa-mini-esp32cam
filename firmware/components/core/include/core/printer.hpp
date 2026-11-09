// What the camera knows about the printer (from PrusaLink, ADR-0012). Pure values.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "core/time.hpp"

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

// "Is the printer doing something worth watching?" Used to upload to PrusaConnect only while
// printing. Fails open: an unknown state (PrusaLink not configured or unreachable) counts as
// active so a broken link never silences the camera. Stays active for `grace` after a print
// ends so the finished part still gets photographed.
class PrintActivity {
 public:
  explicit PrintActivity(Millis grace = Millis{5 * 60'000}) : grace_(grace) {}
  bool active(PrinterPhase phase, Millis now);

 private:
  Millis grace_;
  std::optional<Millis> last_active_;
};

// Latest known printer state; thread-safe, never blocks (served from a cache).
struct PrinterInfo {
  virtual ~PrinterInfo() = default;
  virtual PrinterJob job() const = 0;
};

}  // namespace core
