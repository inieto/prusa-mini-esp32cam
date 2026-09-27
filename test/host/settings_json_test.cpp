#include "core/settings_json.hpp"

#include <gtest/gtest.h>

using namespace core;

TEST(SettingsJson, RoundTripsEveryField) {
  CameraSettings s;
  s.resolution = Resolution::UXGA;
  s.rotation = Rotation::Deg270;
  s.brightness = -2;
  s.gain_ceiling = 6;
  s.flash_on_capture = true;
  s.flash_lead_ms = 1500;
  auto back = merge_camera_settings(CameraSettings{}, camera_settings_to_json(s));
  ASSERT_TRUE(back.has_value());
  EXPECT_EQ(*back, s);
}

TEST(SettingsJson, PartialUpdateKeepsOtherFields) {
  CameraSettings s;
  s.vflip = true;
  auto r = merge_camera_settings(s, R"({"brightness":1,"resolution":"vga"})");
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->brightness, 1);
  EXPECT_EQ(r->resolution, Resolution::VGA);
  EXPECT_TRUE(r->vflip);
}

TEST(SettingsJson, RejectsBadInput) {
  const CameraSettings s;
  EXPECT_FALSE(merge_camera_settings(s, "not json").has_value());
  EXPECT_FALSE(merge_camera_settings(s, R"({"unknown":1})").has_value());
  EXPECT_FALSE(merge_camera_settings(s, R"({"brightness":"1"})").has_value());
  EXPECT_FALSE(merge_camera_settings(s, R"({"brightness":1.5})").has_value());
  EXPECT_FALSE(merge_camera_settings(s, R"({"brightness":3})").has_value());
  EXPECT_FALSE(merge_camera_settings(s, R"({"jpeg_quality":300})").has_value());  // uint8 wrap
  EXPECT_FALSE(merge_camera_settings(s, R"({"rotation":45})").has_value());
  EXPECT_FALSE(merge_camera_settings(s, R"({"resolution":"4k"})").has_value());
  EXPECT_FALSE(merge_camera_settings(s, R"({"hmirror":1})").has_value());
}
