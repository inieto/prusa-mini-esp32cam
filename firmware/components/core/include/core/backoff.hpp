// Exponential backoff with bounded, deterministic (seeded) jitter.
#pragma once

#include <cstdint>

#include "core/time.hpp"

namespace core {

class Backoff {
 public:
  struct Params {
    Millis initial{10'000};
    Millis max{300'000};
    uint32_t multiplier_pct = 200;  // 200 = double each attempt
    uint32_t jitter_pct = 20;       // ± percentage applied to each delay
  };

  Backoff(Params params, uint32_t seed);

  // Delay to wait before the next attempt; advances the attempt counter.
  Millis next();
  void reset();
  uint32_t attempts() const { return attempts_; }

 private:
  uint32_t random();

  Params params_;
  Millis current_;
  uint32_t attempts_ = 0;
  uint32_t state_;
};

}  // namespace core
