#include "core/backoff.hpp"

#include <algorithm>

namespace core {

Backoff::Backoff(Params params, uint32_t seed)
    : params_(params), current_(params.initial), state_(seed != 0 ? seed : 0x9E3779B9u) {}

uint32_t Backoff::random() {  // xorshift32: tiny, deterministic, good enough for jitter
  state_ ^= state_ << 13;
  state_ ^= state_ >> 17;
  state_ ^= state_ << 5;
  return state_;
}

Millis Backoff::next() {
  const int64_t base = current_.count();
  const int64_t spread = base * params_.jitter_pct / 100;
  int64_t delay = base;
  if (spread > 0) {
    delay += static_cast<int64_t>(random() % static_cast<uint32_t>(2 * spread + 1)) - spread;
  }
  const int64_t grown = base * params_.multiplier_pct / 100;
  current_ = std::min(Millis{grown}, params_.max);
  ++attempts_;
  return Millis{std::max<int64_t>(delay, 0)};
}

void Backoff::reset() {
  current_ = params_.initial;
  attempts_ = 0;
}

}  // namespace core
