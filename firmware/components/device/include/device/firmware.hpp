// Firmware lifecycle: OTA updates with automatic rollback, and crash dumps (ADR-0010).
#pragma once

#include <esp_ota_ops.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>

#include "core/error.hpp"

namespace device {

struct FirmwareStatus {
  std::string running_partition;  // "ota_0" / "ota_1"
  std::string version;
  bool pending_verify = false;          // booted a fresh update that has not confirmed health yet
  std::optional<std::string> rolled_back_version;  // an update that failed and was reverted
};
FirmwareStatus firmware_status();

// Marks the running image as good (cancels the rollback). Harmless when not pending.
void confirm_firmware();

// Streams an application image into the inactive OTA slot. Usage: begin → write* → finish.
// Destroying an unfinished writer aborts the update and the running image stays active.
class OtaWriter {
 public:
  OtaWriter() = default;
  ~OtaWriter();
  OtaWriter(const OtaWriter&) = delete;
  OtaWriter& operator=(const OtaWriter&) = delete;

  // size: total image size announced by the client (checked against the slot size).
  core::Result<void> begin(size_t size);
  // The first write must contain at least the image header + app description, which is checked
  // before anything is written: same chip, same project (refuses e.g. the v1.1.2 firmware,
  // which could never roll back to this one).
  core::Result<void> write(std::span<const uint8_t> data);
  // Validates the whole image (SHA-256, segments) and selects it for the next boot.
  core::Result<std::string> finish();  // returns the new version

  static constexpr size_t kHeaderBytes = 288;  // image header + segment header + esp_app_desc_t

 private:
  const esp_partition_t* target_ = nullptr;
  esp_ota_handle_t handle_ = 0;
  bool header_checked_ = false;
  std::string version_;
};

struct CoredumpInfo {
  size_t size = 0;
  std::string task;    // task that crashed
  uint32_t pc = 0;     // program counter of the exception
  std::string reason;  // panic reason as printed by the panic handler
};
std::optional<CoredumpInfo> coredump_info();  // empty when there is no valid dump
// Reads `out.size()` bytes of the dump starting at `offset` (ELF file, as stored in flash).
core::Result<void> read_coredump(size_t offset, std::span<uint8_t> out);
void erase_coredump();

}  // namespace device
