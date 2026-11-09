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

}  // namespace core
