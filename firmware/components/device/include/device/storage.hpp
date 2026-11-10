// SD card actor (ADR-0011): owns all writes to the card. Mounts in 1-bit mode (GPIO4, the flash
// LED, is DATA1), batches the RAM log into daily files with rotation, and saves timelapse frames.
// The HTTP server reads and deletes files directly: FATFS serialises access per volume.
#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "core/ports.hpp"
#include "core/printer.hpp"
#include "core/storage_policy.hpp"

namespace device {

struct SdUsage {
  bool mounted = false;
  uint64_t total_bytes = 0;
  uint64_t free_bytes = 0;
};

struct SdEntry {
  std::string name;
  bool dir = false;
  uint32_t size = 0;
};

class Storage final : public core::FrameObserver {
 public:
  static constexpr const char* kMount = "/sdcard";

  Storage(const core::Clock& clock, const core::WallClock& wall) : clock_(clock), wall_(wall) {}

  // Starts the task; the card is mounted from there (and retried while absent).
  void start(const core::StorageSettings& settings, const core::PrinterInfo* printer);
  void set_settings(const core::StorageSettings& settings);  // thread-safe
  core::StorageSettings settings() const;

  // Capture task: decides about the timelapse and hands the frame over without blocking.
  void on_frame(const core::FrameRef& frame) override;

  SdUsage usage() const;
  // File browser helpers for the HTTP server. Paths are already sanitised and relative.
  core::Result<std::vector<SdEntry>> list(const std::string& rel) const;
  core::Result<void> remove(const std::string& rel);  // file, or folder recursively
  static std::string absolute(const std::string& rel) { return std::string(kMount) + "/" + rel; }

 private:
  static void task_entry(void* self);
  void run();
  bool mount();
  void unmount();
  void flush_log();
  void save_frame(const core::FrameRef& frame, const core::TimelapseTarget& target);
  void rotate_logs();
  void save_coredump();

  const core::Clock& clock_;
  const core::WallClock& wall_;
  const core::PrinterInfo* printer_ = nullptr;
  TaskHandle_t task_ = nullptr;
  std::atomic<bool> mounted_{false};
  void* card_ = nullptr;  // sdmmc_card_t*, kept opaque to avoid the driver header here

  mutable std::mutex mutex_;  // guards the fields below
  core::StorageSettings settings_;
  core::TimelapsePolicy policy_;
  core::FrameRef pending_;  // latest frame waiting to be written (one slot: >= 10 s apart)
  std::optional<core::TimelapseTarget> pending_target_;

  // Owned by the storage task.
  uint64_t log_cursor_ = 0;
  std::string log_file_;
  uint32_t frames_saved_ = 0;
};

}  // namespace device
