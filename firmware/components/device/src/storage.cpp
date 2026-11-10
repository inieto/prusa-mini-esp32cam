#include "device/storage.hpp"

#include <dirent.h>
#include <driver/sdmmc_host.h>
#include <esp_log.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>

#include "device/diagnostics.hpp"
#include "device/firmware.hpp"

namespace device {
namespace {

constexpr const char* kTag = "storage";
constexpr uint32_t kStack = 4096;
constexpr TickType_t kLogFlushPeriod = pdMS_TO_TICKS(10'000);
constexpr TickType_t kMountRetry = pdMS_TO_TICKS(60'000);
constexpr size_t kKeepLogs = 14;  // days
constexpr size_t kMaxListing = 500;

// mkdir -p for a path relative to the mount point.
bool make_dirs(const std::string& rel) {
  std::string path = Storage::kMount;
  size_t i = 0;
  while (i <= rel.size()) {
    const size_t slash = rel.find('/', i);
    const size_t end = slash == std::string::npos ? rel.size() : slash;
    path += "/" + rel.substr(i, end - i);
    if (mkdir(path.c_str(), 0775) != 0 && errno != EEXIST) return false;
    i = end + 1;
  }
  return true;
}

bool remove_tree(const std::string& path) {
  struct stat st;
  if (stat(path.c_str(), &st) != 0) return false;
  if (!S_ISDIR(st.st_mode)) return unlink(path.c_str()) == 0;
  if (DIR* dir = opendir(path.c_str())) {
    while (dirent* e = readdir(dir)) {
      const std::string name = e->d_name;
      if (name == "." || name == "..") continue;
      remove_tree(path + "/" + name);
    }
    closedir(dir);
  }
  return rmdir(path.c_str()) == 0;
}

}  // namespace

void Storage::start(const core::StorageSettings& settings, const core::PrinterInfo* printer) {
  printer_ = printer;
  set_settings(settings);
  xTaskCreatePinnedToCore(task_entry, "storage", kStack, this, 2, &task_, 1);
}

void Storage::set_settings(const core::StorageSettings& settings) {
  std::lock_guard lock(mutex_);
  settings_ = settings;
  policy_.set(settings);
}

core::StorageSettings Storage::settings() const {
  std::lock_guard lock(mutex_);
  return settings_;
}

void Storage::on_frame(const core::FrameRef& frame) {
  if (!mounted_) return;
  const core::PrinterJob job = printer_ ? printer_->job() : core::PrinterJob{};
  {
    std::lock_guard lock(mutex_);
    auto target = policy_.on_frame(clock_.now(), job, wall_.utc_now());
    if (!target) return;
    pending_ = frame;  // replaces an unwritten one: the card is too slow, keep the newest
    pending_target_ = std::move(target);
  }
  if (task_) xTaskNotifyGive(task_);
}

void Storage::task_entry(void* self) { static_cast<Storage*>(self)->run(); }

void Storage::run() {
  TickType_t last_mount_try = 0;
  TickType_t last_flush = xTaskGetTickCount();
  bool first = true;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, kLogFlushPeriod);
    const TickType_t now = xTaskGetTickCount();
    if (!mounted_ && (first || now - last_mount_try >= kMountRetry)) {
      first = false;
      last_mount_try = now;
      if (mount()) {
        rotate_logs();
        save_coredump();
      }
    }
    if (!mounted_) continue;

    core::FrameRef frame;
    std::optional<core::TimelapseTarget> target;
    {
      std::lock_guard lock(mutex_);
      frame = std::move(pending_);
      pending_ = {};
      target.swap(pending_target_);
    }
    if (frame && target) save_frame(frame, *target);
    frame = {};  // give the slot back to the pool before the (slower) log write

    if (now - last_flush >= kLogFlushPeriod) {
      last_flush = now;
      if (settings().log_to_sd) flush_log();
    }
  }
}

bool Storage::mount() {
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.flags = SDMMC_HOST_FLAG_1BIT;
  host.max_freq_khz = SDMMC_FREQ_DEFAULT;
  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.width = 1;  // DATA1 is the flash LED (GPIO4), DATA2 the reset strap (GPIO12)
  slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
  esp_vfs_fat_sdmmc_mount_config_t config = {};
  config.format_if_mount_failed = false;  // never wipe a card the user put in
  config.max_files = 4;
  config.allocation_unit_size = 16 * 1024;
  sdmmc_card_t* card = nullptr;
  const esp_err_t err = esp_vfs_fat_sdmmc_mount(kMount, &host, &slot, &config, &card);
  if (err != ESP_OK) {
    // No card is the normal case: log at info level, once per retry period.
    ESP_LOGI(kTag, "no usable SD card (%s)", esp_err_to_name(err));
    return false;
  }
  card_ = card;
  mounted_ = true;
  const SdUsage u = usage();
  ESP_LOGI(kTag, "SD card mounted: %s, %llu MB free of %llu MB", card->cid.name,
           u.free_bytes >> 20, u.total_bytes >> 20);
  return true;
}

void Storage::unmount() {
  if (!mounted_) return;
  mounted_ = false;
  esp_vfs_fat_sdcard_unmount(kMount, static_cast<sdmmc_card_t*>(card_));
  card_ = nullptr;
  log_file_.clear();
  ESP_LOGW(kTag, "SD card unmounted after a write error (removed?)");
}

void Storage::save_frame(const core::FrameRef& frame, const core::TimelapseTarget& target) {
  if (!make_dirs(target.folder)) {
    unmount();
    return;
  }
  const std::string path = absolute(target.folder + "/" + target.file);
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) {
    unmount();
    return;
  }
  const auto bytes = frame.bytes();
  const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
  if (std::fclose(f) != 0 || !ok) {
    unmount();
    return;
  }
  if (++frames_saved_ % 100 == 1) {
    ESP_LOGI(kTag, "timelapse: %s (%u frames this boot)", path.c_str(),
             static_cast<unsigned>(frames_saved_));
  }
}

void Storage::flush_log() {
  const std::string text = log_read_since(log_cursor_);
  if (text.empty()) return;
  const std::string name = core::log_file_name(wall_.utc_now());
  if (name != log_file_ && !log_file_.empty()) rotate_logs();  // a new day started
  log_file_ = name;
  if (!make_dirs("logs")) {
    unmount();
    return;
  }
  FILE* f = std::fopen(absolute("logs/" + name).c_str(), "a");
  if (!f) {
    unmount();
    return;
  }
  const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
  if (std::fclose(f) != 0 || !ok) unmount();
}

void Storage::rotate_logs() {
  auto entries = list("logs");
  if (!entries) return;
  std::vector<std::string> names;
  for (const SdEntry& e : *entries) {
    if (!e.dir) names.push_back(e.name);
  }
  for (const std::string& old : core::logs_to_delete(std::move(names), kKeepLogs)) {
    unlink(absolute("logs/" + old).c_str());
    ESP_LOGI(kTag, "deleted old log %s", old.c_str());
  }
}

// A panic can only write its dump to the flash partition (no filesystem in the panic handler).
// Once a card is mounted the dump moves here, which frees the partition for the next crash and
// keeps a history instead of only the latest one.
void Storage::save_coredump() {
  const auto dump = coredump_info();
  if (!dump || !make_dirs("coredumps")) return;
  char name[48];
  if (const auto t = wall_.utc_now()) {
    std::snprintf(name, sizeof name, "%04u-%02u-%02u_%02u%02u%02u_%.10s.elf", t->year, t->month,
                  t->day, t->hour, t->minute, t->second, dump->task.c_str());
  } else {
    const auto existing = list("coredumps");
    std::snprintf(name, sizeof name, "undated_%03u_%.10s.elf",
                  static_cast<unsigned>(existing ? existing->size() + 1 : 1), dump->task.c_str());
  }
  const std::string path = absolute(std::string("coredumps/") + name);
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return;
  uint8_t buffer[512];
  bool ok = true;
  for (size_t offset = 0; ok && offset < dump->size;) {
    const size_t n = std::min(sizeof buffer, dump->size - offset);
    ok = read_coredump(offset, {buffer, n}) && std::fwrite(buffer, 1, n, f) == n;
    offset += n;
  }
  ok = std::fclose(f) == 0 && ok;
  if (!ok) {
    unlink(path.c_str());  // keep the dump in flash and try again on the next mount
    return;
  }
  erase_coredump();
  ESP_LOGW(kTag, "crash dump (task %s, %s) moved to %s", dump->task.c_str(), dump->reason.c_str(),
           path.c_str());
}

SdUsage Storage::usage() const {
  SdUsage u;
  u.mounted = mounted_;
  if (u.mounted) esp_vfs_fat_info(kMount, &u.total_bytes, &u.free_bytes);
  return u;
}

core::Result<std::vector<SdEntry>> Storage::list(const std::string& rel) const {
  if (!mounted_) return core::fail(core::Errc::Unsupported);
  const std::string path = rel.empty() ? std::string(kMount) : absolute(rel);
  DIR* dir = opendir(path.c_str());
  if (!dir) return core::fail(core::Errc::NotFound);
  std::vector<SdEntry> out;
  while (dirent* e = readdir(dir)) {
    if (out.size() >= kMaxListing) break;
    SdEntry entry{.name = e->d_name, .dir = e->d_type == DT_DIR};
    if (entry.name == "." || entry.name == "..") continue;
    if (!entry.dir) {
      struct stat st;
      if (stat((path + "/" + entry.name).c_str(), &st) == 0) entry.size = static_cast<uint32_t>(st.st_size);
    }
    out.push_back(std::move(entry));
  }
  closedir(dir);
  return out;
}

core::Result<void> Storage::remove(const std::string& rel) {
  if (!mounted_) return core::fail(core::Errc::Unsupported);
  if (rel.empty()) return core::fail(core::Errc::InvalidArgument);  // never the whole card
  if (!remove_tree(absolute(rel))) return core::fail(core::Errc::NotFound);
  return {};
}

}  // namespace device
