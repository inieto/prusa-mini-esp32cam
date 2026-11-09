// PrusaLink local API (printer's own web server, ADR-0012): pure parsing of its JSON replies.
#pragma once

#include <string_view>

#include "core/error.hpp"
#include "core/printer.hpp"

namespace core::prusalink {

PrinterPhase phase_from_state(std::string_view state);

// GET /api/v1/status → phase, job id, progress, time left (file name is not in this reply).
Result<PrinterJob> parse_status(std::string_view json);

// GET /api/v1/job (200) → copies the display file name into `job`.
Result<void> parse_job_file(std::string_view json, PrinterJob& job);

}  // namespace core::prusalink
