// Fixed pool of JPEG buffers shared as immutable, reference-counted frames (ADR-0003).
// Buffers are supplied by the caller (PSRAM on the device), so the pool never allocates.
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

#include "core/time.hpp"

namespace core {

struct FrameInfo {
  uint16_t width = 0;
  uint16_t height = 0;
  Millis captured_at{0};
  uint32_t sequence = 0;
};

class FramePool;

namespace detail {
struct FrameSlot {
  std::span<uint8_t> storage;
  size_t length = 0;
  FrameInfo info;
  std::atomic<int32_t> refs{0};  // 0 = free, -1 = being written, >0 = readers
};
}  // namespace detail

// Read-only handle. Copying shares the frame; the slot is released with the last handle.
class FrameRef {
 public:
  FrameRef() = default;
  FrameRef(const FrameRef& other);
  FrameRef(FrameRef&& other) noexcept : slot_(std::exchange(other.slot_, nullptr)) {}
  FrameRef& operator=(FrameRef other) noexcept;
  ~FrameRef();

  explicit operator bool() const { return slot_ != nullptr; }
  std::span<const uint8_t> bytes() const;
  const FrameInfo& info() const { return slot_->info; }

 private:
  friend class FrameWriter;
  explicit FrameRef(detail::FrameSlot* slot) : slot_(slot) {}
  detail::FrameSlot* slot_ = nullptr;
};

// Exclusive write access to a free slot. Dropping it without commit() frees the slot.
class FrameWriter {
 public:
  FrameWriter(FrameWriter&& other) noexcept : slot_(std::exchange(other.slot_, nullptr)) {}
  FrameWriter(const FrameWriter&) = delete;
  FrameWriter& operator=(const FrameWriter&) = delete;
  FrameWriter& operator=(FrameWriter&&) = delete;
  ~FrameWriter();

  std::span<uint8_t> buffer() const { return slot_->storage; }
  FrameRef commit(size_t length, const FrameInfo& info) &&;

 private:
  friend class FramePool;
  explicit FrameWriter(detail::FrameSlot* slot) : slot_(slot) {}
  detail::FrameSlot* slot_;
};

class FramePool {
 public:
  static constexpr size_t kMaxSlots = 4;

  // Each span becomes one slot; at most kMaxSlots. The pool must outlive every FrameRef.
  explicit FramePool(std::span<const std::span<uint8_t>> buffers);
  FramePool(const FramePool&) = delete;
  FramePool& operator=(const FramePool&) = delete;

  std::optional<FrameWriter> acquire();
  size_t capacity() const { return count_; }
  size_t in_use() const;
  size_t slot_size() const { return count_ ? slots_[0].storage.size() : 0; }

 private:
  std::array<detail::FrameSlot, kMaxSlots> slots_{};
  size_t count_ = 0;
};

}  // namespace core
