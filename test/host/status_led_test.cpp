#include "core/status_led.hpp"

#include <gtest/gtest.h>

using namespace core;

TEST(StatusLed, DarkWhenAllIsWell) {
  EXPECT_EQ(status_pattern(true, false, ConnectState::Online), LedPattern::Off);
  EXPECT_EQ(status_pattern(true, false, ConnectState::NotConfigured), LedPattern::Off);
  for (uint32_t t = 0; t < 32; ++t) EXPECT_FALSE(led_on(LedPattern::Off, t));
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
