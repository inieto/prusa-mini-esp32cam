#include "core/prusalink.hpp"

#include <cJSON.h>

#include <memory>

namespace core::prusalink {
namespace {

struct JsonDeleter {
  void operator()(cJSON* j) const { cJSON_Delete(j); }
};
using JsonPtr = std::unique_ptr<cJSON, JsonDeleter>;

double number(const cJSON* obj, const char* key, double fallback = 0) {
  const cJSON* v = cJSON_GetObjectItemCaseSensitive(obj, key);
  return cJSON_IsNumber(v) ? v->valuedouble : fallback;
}

}  // namespace

PrinterPhase phase_from_state(std::string_view s) {
  if (s == "PRINTING") return PrinterPhase::Printing;
  if (s == "PAUSED") return PrinterPhase::Paused;
  if (s == "FINISHED" || s == "STOPPED") return PrinterPhase::Finished;
  if (s == "IDLE" || s == "READY") return PrinterPhase::Idle;
  if (s == "BUSY") return PrinterPhase::Busy;
  if (s == "ERROR" || s == "ATTENTION") return PrinterPhase::Error;
  return PrinterPhase::Unknown;
}

Result<PrinterJob> parse_status(std::string_view json) {
  JsonPtr root(cJSON_ParseWithLength(json.data(), json.size()));
  if (!root) return fail(Errc::InvalidArgument);
  const cJSON* printer = cJSON_GetObjectItemCaseSensitive(root.get(), "printer");
  const cJSON* state = cJSON_GetObjectItemCaseSensitive(printer, "state");
  if (!cJSON_IsString(state)) return fail(Errc::InvalidArgument);  // the only required field

  PrinterJob job;
  job.phase = phase_from_state(state->valuestring);
  if (const cJSON* j = cJSON_GetObjectItemCaseSensitive(root.get(), "job"); cJSON_IsObject(j)) {
    const double id = number(j, "id");
    job.job_id = id > 0 && id < 4e9 ? static_cast<uint32_t>(id) : 0;
    const double progress = number(j, "progress");
    job.progress = progress < 0 ? 0.f : progress > 100 ? 100.f : static_cast<float>(progress);
    const double left = number(j, "time_remaining");
    job.time_left_s = left > 0 && left < 4e9 ? static_cast<uint32_t>(left) : 0;
  }
  return job;
}

Result<void> parse_job_file(std::string_view json, PrinterJob& job) {
  JsonPtr root(cJSON_ParseWithLength(json.data(), json.size()));
  if (!root) return fail(Errc::InvalidArgument);
  const cJSON* file = cJSON_GetObjectItemCaseSensitive(root.get(), "file");
  const cJSON* name = cJSON_GetObjectItemCaseSensitive(file, "display_name");
  if (!cJSON_IsString(name)) name = cJSON_GetObjectItemCaseSensitive(file, "name");
  if (!cJSON_IsString(name)) return fail(Errc::NotFound);  // e.g. a serial (OctoPrint-style) print
  job.file = std::string(name->valuestring).substr(0, 96);
  return {};
}

}  // namespace core::prusalink
