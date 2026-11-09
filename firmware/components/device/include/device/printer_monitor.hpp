// Polls the printer's PrusaLink API (ADR-0012) from its own task and caches the result, so the
// capture task and the web server read the printer state without ever waiting on the network.
#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <mutex>
#include <string>

#include "core/printer.hpp"
#include "core/error.hpp"

namespace device {

struct PrinterSettings {
  std::string host;             // "192.168.0.50" or "mini.local[:port]"; empty = disabled
  std::string user = "maker";   // PrusaLink digest user (printer menu: Settings → Network)
  std::string password;
  bool upload_only_while_printing = false;
};

class PrinterMonitor final : public core::PrinterInfo {
 public:
  void start(const PrinterSettings& settings);
  void set_settings(const PrinterSettings& settings);  // thread-safe, polls again right away
  PrinterSettings settings() const;

  core::PrinterJob job() const override;  // Unknown while disabled or unreachable
  bool reachable() const { return reachable_; }
  std::string last_error() const;

  static bool valid_host(std::string_view host);

 private:
  static void task_entry(void* self);
  void run();
  void poll(const PrinterSettings& s);
  core::Result<std::string> get(const PrinterSettings& s, const char* path, int* status);
  void set_error(std::string message);

  TaskHandle_t task_ = nullptr;
  std::atomic<bool> reachable_{false};
  mutable std::mutex mutex_;  // guards the fields below
  PrinterSettings settings_;
  core::PrinterJob job_;
  std::string error_;
  uint32_t failures_ = 0;
};

}  // namespace device
