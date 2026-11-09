#include "core/printer.hpp"

namespace core {

std::string_view to_string(PrinterPhase phase) {
  switch (phase) {
    case PrinterPhase::Unknown: return "unknown";
    case PrinterPhase::Idle: return "idle";
    case PrinterPhase::Busy: return "busy";
    case PrinterPhase::Printing: return "printing";
    case PrinterPhase::Paused: return "paused";
    case PrinterPhase::Finished: return "finished";
    case PrinterPhase::Error: return "error";
  }
  return "unknown";
}

bool PrintActivity::active(PrinterPhase phase, Millis now) {
  switch (phase) {
    case PrinterPhase::Unknown:
    case PrinterPhase::Busy:
    case PrinterPhase::Printing:
    case PrinterPhase::Paused:
    case PrinterPhase::Error:  // a failed print is exactly what someone wants to see
      last_active_ = now;
      return true;
    case PrinterPhase::Idle:
    case PrinterPhase::Finished:
      return last_active_ && now - *last_active_ < grace_;
  }
  return true;
}

}  // namespace core
