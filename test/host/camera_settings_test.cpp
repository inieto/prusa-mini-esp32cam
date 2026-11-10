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

TEST(Framing, OneXIsTheWholeSensorCentered) {
  const CameraSettings s;
  EXPECT_EQ(framing_window({2048, 1536}, {800, 600}, s), (SensorWindow{0, 0, 2048, 1536}));
}

TEST(Framing, ZoomShrinksTheWindowAndPanMovesIt) {
  CameraSettings s;
  s.zoom_pct = 200;
  EXPECT_EQ(framing_window({2048, 1536}, {800, 600}, s), (SensorWindow{512, 384, 1024, 768}));
  s.pan_x = 100;   // all the way right
  s.pan_y = -100;  // all the way up
  EXPECT_EQ(framing_window({2048, 1536}, {800, 600}, s), (SensorWindow{1024, 0, 1024, 768}));
}

TEST(Framing, NeverUpscales) {
  EXPECT_EQ(max_zoom_pct({2048, 1536}, {800, 600}), 256);
  EXPECT_EQ(max_zoom_pct({1600, 1200}, {800, 600}), 200);
  EXPECT_EQ(max_zoom_pct({1600, 1200}, {1600, 1200}), 100);
  CameraSettings s;
  s.zoom_pct = 400;  // clamped to 2x for an OV2640 at SVGA
  EXPECT_EQ(framing_window({1600, 1200}, {800, 600}, s), (SensorWindow{400, 296, 800, 600}));
}

TEST(Framing, KeepsTheOutputAspectRatio) {
  CameraSettings s;  // SXGA is 5:4 on a 4:3 sensor: full height, narrower window
  const SensorWindow w = framing_window({2048, 1536}, {1280, 1024}, s);
  EXPECT_EQ(w.height, 1536);
  EXPECT_EQ(w.width, 1920);
  EXPECT_EQ(w.x, 64);
}

TEST(Framing, PanFollowsWhatTheUserSees) {
  CameraSettings s;
  s.zoom_pct = 200;
  s.pan_x = 100;  // "right" in the final image
  s.hmirror = true;  // the sensor mirrors: right in the image is left on the array
  EXPECT_EQ(framing_window({2048, 1536}, {800, 600}, s).x, 0);
  s.hmirror = false;
  s.rotation = Rotation::Deg90;  // viewer rotates 90° clockwise: image right = array up
  const SensorWindow w = framing_window({2048, 1536}, {800, 600}, s);
  EXPECT_EQ(w.x, 512);
  EXPECT_EQ(w.y, 0);
}

TEST(CameraSettings, ValidatesSensorExtras) {
  CameraSettings s;
  s.sharpness = -4;
  EXPECT_FALSE(validate(s).has_value());
  s = {};
  s.denoise = 9;
  EXPECT_FALSE(validate(s).has_value());
  s = {};
  s.wb_mode = 5;
  EXPECT_FALSE(validate(s).has_value());
}

TEST(Framing, ValidatesRanges) {
  CameraSettings s;
  s.zoom_pct = 99;
  EXPECT_FALSE(validate(s).has_value());
  s = {};
  s.pan_x = 101;
  EXPECT_FALSE(validate(s).has_value());
}
