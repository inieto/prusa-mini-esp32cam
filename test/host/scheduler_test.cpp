#include "core/scheduler.hpp"

#include <gtest/gtest.h>

using core::Millis;
using core::SnapshotScheduler;

TEST(Scheduler, FirstCaptureIsImmediateThenEveryInterval) {
  SnapshotScheduler s(Millis{30'000});
  EXPECT_TRUE(s.due(Millis{0}));
  s.schedule_after(Millis{0});
  EXPECT_FALSE(s.due(Millis{29'999}));
  EXPECT_TRUE(s.due(Millis{30'000}));
}

TEST(Scheduler, DelayOverridesInterval) {
  SnapshotScheduler s(Millis{10'000});
  s.schedule_after(Millis{0}, Millis{60'000});
  EXPECT_FALSE(s.due(Millis{10'000}));
  EXPECT_TRUE(s.due(Millis{60'000}));
}

TEST(Scheduler, ManualModeOnlyFiresOnTrigger) {
  SnapshotScheduler s(std::nullopt);
  EXPECT_FALSE(s.due(Millis{1'000'000}));
  s.trigger_now();
  EXPECT_TRUE(s.due(Millis{0}));
  s.schedule_after(Millis{0});
  EXPECT_FALSE(s.due(Millis{1'000'000}));
}

TEST(Scheduler, ShorterIntervalAppliesImmediatelyLongerWaitsCurrentSlot) {
  SnapshotScheduler s(Millis{60'000});
  s.schedule_after(Millis{0});  // next at 60 s
  s.set_interval(Millis{10'000}, Millis{5'000});
  EXPECT_EQ(s.next_at(), Millis{15'000});
  s.set_interval(Millis{120'000}, Millis{6'000});
  EXPECT_EQ(s.next_at(), Millis{15'000});
}
