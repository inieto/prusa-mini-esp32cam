#include "core/camera_settings.hpp"

#include <gtest/gtest.h>

using namespace core;

TEST(CameraSettings, DefaultsAreValid) { EXPECT_TRUE(validate(CameraSettings{}).has_value()); }

TEST(CameraSettings, RejectsOutOfRange) {
  CameraSettings s;
  s.jpeg_quality = 9;
  EXPECT_FALSE(validate(s).has_value());
  s = {};
  s.gain_ceiling = 7;
  EXPECT_FALSE(validate(s).has_value());
  s = {};
  s.brightness = 3;
  EXPECT_FALSE(validate(s).has_value());
  s = {};
  s.resolution = static_cast<Resolution>(8);
  EXPECT_FALSE(validate(s).has_value());
}

TEST(CameraSettings, ResolutionTableRoundTrips) {
  for (Resolution r : all_resolutions()) EXPECT_EQ(resolution_for(size_of(r)), r);
  EXPECT_EQ(resolution_for({123, 45}), std::nullopt);
  EXPECT_EQ(size_of(Resolution::UXGA), (Size{1600, 1200}));
}

TEST(CameraSettings, FrameBudgetFitsThreeSlotsInPsram) {
  // Worst case (OV3660 at QXGA): 3 slots + one driver buffer must leave ~0.8 MB of the 4 MB.
  EXPECT_LT(3 * max_jpeg_size(Resolution::QXGA) + driver_jpeg_buffer(Resolution::QXGA),
            3300u * 1024);
  // SVGA buffer must hold the ~100 KB OV3660 frames that overflowed the driver default.
  EXPECT_GT(driver_jpeg_buffer(Resolution::SVGA), 150u * 1024);
  EXPECT_GT(max_jpeg_size(Resolution::VGA), 12u * 1024);  // > the 12.9 KB we observed
  EXPECT_EQ(size_of(Resolution::QXGA), (Size{2048, 1536}));
}
