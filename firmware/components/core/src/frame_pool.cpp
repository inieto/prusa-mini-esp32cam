#include "core/frame_pool.hpp"

#include <algorithm>

namespace core {

FrameRef::FrameRef(const FrameRef& other) : slot_(other.slot_) {
  if (slot_) slot_->refs.fetch_add(1, std::memory_order_relaxed);
}

FrameRef& FrameRef::operator=(FrameRef other) noexcept {
  std::swap(slot_, other.slot_);
  return *this;
}

FrameRef::~FrameRef() {
  // Reaching zero marks the slot free; acquire() can then claim it.
  if (slot_) slot_->refs.fetch_sub(1, std::memory_order_acq_rel);
}

std::span<const uint8_t> FrameRef::bytes() const {
  return {slot_->storage.data(), slot_->length};
}

FrameWriter::~FrameWriter() {
  if (slot_) slot_->refs.store(0, std::memory_order_release);
}

FrameRef FrameWriter::commit(size_t length, const FrameInfo& info) && {
  slot_->length = std::min(length, slot_->storage.size());
  slot_->info = info;
  slot_->refs.store(1, std::memory_order_release);
  return FrameRef(std::exchange(slot_, nullptr));
}

FramePool::FramePool(std::span<const std::span<uint8_t>> buffers)
    : count_(std::min(buffers.size(), kMaxSlots)) {
  for (size_t i = 0; i < count_; ++i) slots_[i].storage = buffers[i];
}

std::optional<FrameWriter> FramePool::acquire() {
  for (size_t i = 0; i < count_; ++i) {
    int32_t expected = 0;
    if (slots_[i].refs.compare_exchange_strong(expected, -1, std::memory_order_acq_rel)) {
      return FrameWriter(&slots_[i]);
    }
  }
  return std::nullopt;
}

size_t FramePool::in_use() const {
  size_t used = 0;
  for (size_t i = 0; i < count_; ++i) {
    if (slots_[i].refs.load(std::memory_order_acquire) != 0) ++used;
  }
  return used;
}

}  // namespace core
