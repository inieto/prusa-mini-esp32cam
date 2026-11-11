#include "core/status_led.hpp"

#include <gtest/gtest.h>

using namespace core;

TEST(StatusLed, OneBlipPerMinuteWhenAllIsWell) {
  EXPECT_EQ(status_pattern(true, false, ConnectState::Online), LedPattern::Heartbeat);
  EXPECT_EQ(status_pattern(true, false, ConnectState::NotConfigured), LedPattern::Heartbeat);
  int on = 0;
  for (uint32_t t = 0; t < 480 * 3; ++t) on += led_on(LedPattern::Heartbeat, t);
  EXPECT_EQ(on, 3 * 4);  // three minutes, three 500 ms blinks (4 ticks of 125 ms each)
  EXPECT_TRUE(led_on(LedPattern::Heartbeat, 480 + 3));
  EXPECT_FALSE(led_on(LedPattern::Heartbeat, 480 + 4));
}

TEST(StatusLed, SlowBlinkWithoutWifiOrOnTheSetupAp) {
  EXPECT_EQ(status_pattern(false, false, ConnectState::Online), LedPattern::SlowBlink);
  EXPECT_EQ(status_pattern(true, true, ConnectState::Online), LedPattern::SlowBlink);
  int on = 0;
  for (uint32_t t = 0; t < 16; ++t) on += led_on(LedPattern::SlowBlink, t);
  EXPECT_EQ(on, 8);
}

TEST(StatusLed, DoubleBlinkWhenUploadsFail) {
  EXPECT_EQ(status_pattern(true, false, ConnectState::Offline), LedPattern::DoubleBlink);
  EXPECT_EQ(status_pattern(true, false, ConnectState::TokenRejected), LedPattern::DoubleBlink);
  EXPECT_TRUE(led_on(LedPattern::DoubleBlink, 0));
  EXPECT_FALSE(led_on(LedPattern::DoubleBlink, 1));
  EXPECT_TRUE(led_on(LedPattern::DoubleBlink, 18));  // repeats every 16 ticks
}
