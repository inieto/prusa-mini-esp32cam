#include "core/backoff.hpp"

#include <gtest/gtest.h>

using core::Backoff;
using core::Millis;

TEST(Backoff, DoublesUpToMaxWithoutJitter) {
  Backoff b({.initial = Millis{10'000}, .max = Millis{60'000}, .jitter_pct = 0}, 1);
  EXPECT_EQ(b.next(), Millis{10'000});
  EXPECT_EQ(b.next(), Millis{20'000});
  EXPECT_EQ(b.next(), Millis{40'000});
  EXPECT_EQ(b.next(), Millis{60'000});
  EXPECT_EQ(b.next(), Millis{60'000});
  EXPECT_EQ(b.attempts(), 5u);
}

TEST(Backoff, ResetStartsOver) {
  Backoff b({.jitter_pct = 0}, 1);
  b.next();
  b.next();
  b.reset();
  EXPECT_EQ(b.attempts(), 0u);
  EXPECT_EQ(b.next(), Millis{10'000});
}

TEST(Backoff, JitterStaysWithinBoundsAndIsDeterministic) {
  Backoff a({.initial = Millis{10'000}, .max = Millis{10'000}, .jitter_pct = 20}, 42);
  Backoff b({.initial = Millis{10'000}, .max = Millis{10'000}, .jitter_pct = 20}, 42);
  for (int i = 0; i < 1000; ++i) {
    const Millis d = a.next();
    EXPECT_GE(d, Millis{8'000});
    EXPECT_LE(d, Millis{12'000});
    EXPECT_EQ(d, b.next());
  }
}
