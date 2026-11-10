// The last few KB of log, kept in memory that survives resets but not power loss (RTC slow
// memory on the ESP32, documented to keep RTC_NOINIT data across software resets, panics and
// watchdogs). After a crash the next boot can show what happened right before it. Pure.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace core {

class LogTail {
 public:
  static constexpr size_t kCapacity = 6144;

  // Plain data so it can be placed in a no-init section; its contents are untrusted at boot.
  struct Storage {
    uint32_t magic;
    uint32_t head;     // next write position
    uint32_t wrapped;  // 0 or 1
    uint32_t check;    // derived from the fields above: garbage rarely matches
    char data[kCapacity];
  };

  explicit LogTail(Storage& storage) : s_(storage) {}

  // At boot: the previous run's text (oldest first) if the storage is intact and `trust` is set
  // (false after a power-on, when the memory holds random bits), then starts empty.
  std::string take_previous(bool trust);

  // Called from the log hook (inside a critical section): no allocation, no locking.
  void append(std::string_view text);

 private:
  static uint32_t check_of(const Storage& s);
  Storage& s_;
};

}  // namespace core
