#include "device/printer_monitor.hpp"

#include <esp_http_client.h>
#include <esp_log.h>

#include <cctype>

#include "core/prusalink.hpp"

namespace device {
namespace {

constexpr const char* kTag = "printer";
constexpr uint32_t kStack = 6144;  // esp_http_client + digest (MD5) on the stack
constexpr TickType_t kPollPeriod = pdMS_TO_TICKS(10'000);
constexpr TickType_t kSlowPoll = pdMS_TO_TICKS(60'000);  // printer off: do not spam the LAN
constexpr uint32_t kFailuresBeforeUnknown = 3;
constexpr size_t kMaxBody = 4096;
constexpr int kTimeoutMs = 4000;

esp_err_t collect(esp_http_client_event_t* ev) {
  auto* body = static_cast<std::string*>(ev->user_data);
  if (ev->event_id == HTTP_EVENT_ON_DATA && body && body->size() + ev->data_len <= kMaxBody) {
    body->append(static_cast<const char*>(ev->data), ev->data_len);
  }
  return ESP_OK;
}

}  // namespace

bool PrinterMonitor::valid_host(std::string_view host) {
  if (host.empty() || host.size() > 64) return false;
  for (char c : host) {
    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == ':')) return false;
  }
  return true;
}

void PrinterMonitor::start(const PrinterSettings& settings) {
  {
    std::lock_guard lock(mutex_);
    settings_ = settings;
  }
  xTaskCreatePinnedToCore(task_entry, "printer", kStack, this, 3, &task_, 1);
}

void PrinterMonitor::set_settings(const PrinterSettings& settings) {
  {
    std::lock_guard lock(mutex_);
    settings_ = settings;
    job_ = {};
    error_.clear();
    failures_ = 0;
  }
  reachable_ = false;
  if (task_) xTaskNotifyGive(task_);
}

PrinterSettings PrinterMonitor::settings() const {
  std::lock_guard lock(mutex_);
  return settings_;
}

core::PrinterJob PrinterMonitor::job() const {
  std::lock_guard lock(mutex_);
  return job_;
}

std::string PrinterMonitor::last_error() const {
  std::lock_guard lock(mutex_);
  return error_;
}

void PrinterMonitor::set_error(std::string message) {
  std::lock_guard lock(mutex_);
  if (++failures_ >= kFailuresBeforeUnknown) {
    job_ = {};  // fail open: Unknown means "assume active" for the upload gate
    reachable_ = false;
  }
  if (message != error_) ESP_LOGW(kTag, "PrusaLink: %s", message.c_str());
  error_ = std::move(message);
}

void PrinterMonitor::task_entry(void* self) { static_cast<PrinterMonitor*>(self)->run(); }

void PrinterMonitor::run() {
  for (;;) {
    const PrinterSettings s = settings();
    if (!s.host.empty()) poll(s);
    uint32_t failures;
    {
      std::lock_guard lock(mutex_);
      failures = failures_;
    }
    ulTaskNotifyTake(pdTRUE, failures >= kFailuresBeforeUnknown ? kSlowPoll : kPollPeriod);
  }
}

core::Result<std::string> PrinterMonitor::get(const PrinterSettings& s, const char* path,
                                              int* status) {
  const std::string url = "http://" + s.host + path;
  std::string body;
  esp_http_client_config_t config = {};
  config.url = url.c_str();
  config.timeout_ms = kTimeoutMs;
  config.username = s.user.c_str();
  config.password = s.password.c_str();
  config.auth_type = HTTP_AUTH_TYPE_DIGEST;  // perform() answers the 401 challenge itself
  config.event_handler = collect;
  config.user_data = &body;
  config.disable_auto_redirect = true;
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) return core::fail(core::Errc::NoMemory);
  const esp_err_t err = esp_http_client_perform(client);
  *status = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);
  if (err != ESP_OK) return core::fail(core::Errc::Transport, err);
  return body;
}

void PrinterMonitor::poll(const PrinterSettings& s) {
  int status = 0;
  const auto body = get(s, "/api/v1/status", &status);
  if (!body) {
    set_error("printer unreachable");
    return;
  }
  if (status == 401 || status == 403) {
    set_error("wrong PrusaLink user or password");
    return;
  }
  if (status != 200) {
    set_error("unexpected HTTP " + std::to_string(status) + " (PrusaLink too old?)");
    return;
  }
  auto job = core::prusalink::parse_status(*body);
  if (!job) {
    set_error("unexpected reply (not PrusaLink?)");
    return;
  }

  core::PrinterJob previous = this->job();
  if (job->job_id != 0 && job->job_id == previous.job_id) {
    job->file = previous.file;  // the file name only changes with the job
  } else if (job->job_id != 0) {
    int job_status = 0;
    if (const auto j = get(s, "/api/v1/job", &job_status); j && job_status == 200) {
      core::prusalink::parse_job_file(*j, *job);
    }
  }
  if (job->phase != previous.phase || job->job_id != previous.job_id) {
    ESP_LOGI(kTag, "printer %s%s%s", std::string(core::to_string(job->phase)).c_str(),
             job->file.empty() ? "" : ": ", job->file.c_str());
  }
  std::lock_guard lock(mutex_);
  job_ = std::move(*job);
  failures_ = 0;
  error_.clear();
  reachable_ = true;
}

}  // namespace device
