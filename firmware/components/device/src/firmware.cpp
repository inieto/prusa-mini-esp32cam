#include "device/firmware.hpp"

#include <esp_app_desc.h>
#include <esp_core_dump.h>
#include <esp_image_format.h>
#include <esp_log.h>
#include <esp_partition.h>

#include <cstring>

namespace device {
namespace {

constexpr const char* kTag = "firmware";
static_assert(OtaWriter::kHeaderBytes == sizeof(esp_image_header_t) +
                                         sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t));

std::string version_of(const esp_partition_t* partition) {
  esp_app_desc_t desc;
  if (!partition || esp_ota_get_partition_description(partition, &desc) != ESP_OK) return {};
  return desc.version;
}

}  // namespace

FirmwareStatus firmware_status() {
  FirmwareStatus s;
  const esp_partition_t* running = esp_ota_get_running_partition();
  s.running_partition = running ? running->label : "?";
  s.version = esp_app_get_description()->version;
  esp_ota_img_states_t state;
  s.pending_verify = running && esp_ota_get_state_partition(running, &state) == ESP_OK &&
                     state == ESP_OTA_IMG_PENDING_VERIFY;
  if (const esp_partition_t* invalid = esp_ota_get_last_invalid_partition()) {
    const std::string v = version_of(invalid);
    s.rolled_back_version = v.empty() ? std::string("unknown") : v;
  }
  return s;
}

void confirm_firmware() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  if (running && esp_ota_get_state_partition(running, &state) == ESP_OK &&
      state == ESP_OTA_IMG_PENDING_VERIFY) {
    esp_ota_mark_app_valid_cancel_rollback();
    ESP_LOGI(kTag, "update confirmed healthy, rollback cancelled");
  }
}

OtaWriter::~OtaWriter() {
  if (handle_) esp_ota_abort(handle_);
}

core::Result<void> OtaWriter::begin(size_t size) {
  target_ = esp_ota_get_next_update_partition(nullptr);
  if (!target_) return core::fail(core::Errc::Io);
  if (size < kHeaderBytes || size > target_->size) return core::fail(core::Errc::OutOfRange);
  // Sequential mode erases sector by sector while writing instead of the whole slot up front
  // (that takes seconds and would stall the HTTP task).
  const esp_err_t err = esp_ota_begin(target_, OTA_WITH_SEQUENTIAL_WRITES, &handle_);
  if (err != ESP_OK) {
    handle_ = 0;
    return core::fail(core::Errc::Io, err);
  }
  ESP_LOGI(kTag, "receiving %u bytes into %s", static_cast<unsigned>(size), target_->label);
  return {};
}

core::Result<void> OtaWriter::write(std::span<const uint8_t> data) {
  if (!handle_) return core::fail(core::Errc::InvalidArgument);
  if (!header_checked_) {
    if (data.size() < kHeaderBytes) return core::fail(core::Errc::InvalidArgument);
    esp_image_header_t image;
    std::memcpy(&image, data.data(), sizeof image);
    esp_app_desc_t desc;
    std::memcpy(&desc, data.data() + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t),
                sizeof desc);
    const esp_app_desc_t* self = esp_app_get_description();
    if (image.magic != ESP_IMAGE_HEADER_MAGIC || desc.magic_word != ESP_APP_DESC_MAGIC_WORD ||
        image.chip_id != ESP_CHIP_ID_ESP32) {
      ESP_LOGW(kTag, "rejected: not an ESP32 application image");
      return core::fail(core::Errc::InvalidArgument);
    }
    if (std::strncmp(desc.project_name, self->project_name, sizeof desc.project_name) != 0) {
      ESP_LOGW(kTag, "rejected: image is \"%.32s\", not \"%s\"", desc.project_name,
               self->project_name);
      return core::fail(core::Errc::Unsupported);
    }
    version_.assign(desc.version, strnlen(desc.version, sizeof desc.version));
    header_checked_ = true;
  }
  const esp_err_t err = esp_ota_write(handle_, data.data(), data.size());
  if (err != ESP_OK) return core::fail(core::Errc::Io, err);
  return {};
}

core::Result<std::string> OtaWriter::finish() {
  if (!handle_ || !header_checked_) return core::fail(core::Errc::InvalidArgument);
  const esp_err_t err = esp_ota_end(handle_);
  handle_ = 0;  // esp_ota_end releases the handle even on failure
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "image validation failed: %s", esp_err_to_name(err));
    return core::fail(core::Errc::InvalidArgument, err);
  }
  if (const esp_err_t e = esp_ota_set_boot_partition(target_); e != ESP_OK) {
    return core::fail(core::Errc::Io, e);
  }
  ESP_LOGW(kTag, "update %s written to %s; it must confirm health after boot or it rolls back",
           version_.c_str(), target_->label);
  return version_;
}

std::optional<CoredumpInfo> coredump_info() {
  size_t addr = 0, size = 0;
  if (esp_core_dump_image_check() != ESP_OK || esp_core_dump_image_get(&addr, &size) != ESP_OK) {
    return std::nullopt;
  }
  CoredumpInfo info;
  info.size = size;
  esp_core_dump_summary_t summary;
  if (esp_core_dump_get_summary(&summary) == ESP_OK) {
    info.task.assign(summary.exc_task, strnlen(summary.exc_task, sizeof summary.exc_task));
    info.pc = summary.exc_pc;
  }
  char reason[96] = {};
  if (esp_core_dump_get_panic_reason(reason, sizeof reason) == ESP_OK) info.reason = reason;
  return info;
}

core::Result<void> read_coredump(size_t offset, std::span<uint8_t> out) {
  size_t addr = 0, size = 0;
  if (esp_core_dump_image_get(&addr, &size) != ESP_OK) return core::fail(core::Errc::NotFound);
  if (offset + out.size() > size) return core::fail(core::Errc::OutOfRange);
  const esp_partition_t* part =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, nullptr);
  if (!part) return core::fail(core::Errc::NotFound);
  const esp_err_t err = esp_partition_read(part, addr - part->address + offset, out.data(), out.size());
  if (err != ESP_OK) return core::fail(core::Errc::Io, err);
  return {};
}

void erase_coredump() {
  esp_core_dump_image_erase();
  ESP_LOGI(kTag, "core dump erased");
}

}  // namespace device
