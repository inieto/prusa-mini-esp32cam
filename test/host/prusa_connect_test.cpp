#include "core/prusa_connect.hpp"

#include <gtest/gtest.h>

using namespace core::connect;

TEST(PrusaConnect, LegacyFingerprintMatchesDeployedCamera) {
  // Value read from the user's camera running v1.1.2 (MAC F8:B3:B7:A8:45:84).
  EXPECT_EQ(legacy_fingerprint({0xF8, 0xB3, 0xB7, 0xA8, 0x45, 0x84}),
            "MjQ4MTc5MTgzMTY4NjkxMzIgMDA6MDA6MDA6MDA6MDA6MDA=");
}

TEST(PrusaConnect, TokenValidation) {
  EXPECT_TRUE(is_valid_token("RyUXuOLc2N0Ttj572aq5"));
  EXPECT_FALSE(is_valid_token("RyUXuOLc2N0Ttj572aq"));     // 19 chars
  EXPECT_FALSE(is_valid_token("RyUXuOLc2N0Ttj572aq5X"));   // 21 chars
  EXPECT_FALSE(is_valid_token("RyUXuOLc2N0Ttj572a\r\n"));  // header injection
}

TEST(PrusaConnect, FingerprintValidation) {
  EXPECT_TRUE(is_valid_fingerprint("MjQ4MTc5MTgzMTY4NjkxMzIgMDA6MDA6MDA6MDA6MDA6MDA="));
  EXPECT_FALSE(is_valid_fingerprint("short"));
  EXPECT_FALSE(is_valid_fingerprint(std::string(65, 'a')));
  EXPECT_FALSE(is_valid_fingerprint("abcdefghijklmnop\r\nX-Evil: 1"));
}

TEST(PrusaConnect, TriggerSchemes) {
  EXPECT_EQ(parse_trigger_scheme("THIRTY_SEC"), TriggerScheme::ThirtySec);
  EXPECT_EQ(parse_trigger_scheme("bogus"), std::nullopt);
  EXPECT_EQ(interval_for(TriggerScheme::TenSec), core::Millis{10'000});
  EXPECT_EQ(interval_for(TriggerScheme::EachLayer), std::nullopt);
}

TEST(PrusaConnect, InfoJsonHasRequiredFieldsAndEscapes) {
  CameraIdentity id{.name = "cam \"1\"", .firmware = "0.1.0"};
  NetworkSnapshot net{.connected = true, .ipv4 = "192.168.0.61", .ssid = "a\\b",
                      .mac = "F8:B3:B7:A8:45:84"};
  const std::string json = build_info_json(id, net, {800, 600});
  EXPECT_NE(json.find(R"("name":"cam \"1\"")"), std::string::npos);
  EXPECT_NE(json.find(R"("resolution":{"width":800,"height":600})"), std::string::npos);
  EXPECT_NE(json.find(R"("wifi_ssid":"a\\b")"), std::string::npos);
  EXPECT_NE(json.find(R"("capabilities":["trigger_scheme"])"), std::string::npos);
  EXPECT_EQ(json.find("trigger_scheme\":\""), std::string::npos) << "must not override user choice";
}

TEST(PrusaConnect, ParsesInfoResponse) {
  auto r = parse_info_response(
      R"({"id":7,"name":"prusa-esp32cam","config":{"name":"x","trigger_scheme":"SIXTY_SEC"}})");
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->name, "prusa-esp32cam");
  EXPECT_EQ(r->trigger_scheme, TriggerScheme::SixtySec);

  EXPECT_FALSE(parse_info_response("not json").has_value());
  auto empty = parse_info_response("{}");
  ASSERT_TRUE(empty.has_value());
  EXPECT_EQ(empty->trigger_scheme, std::nullopt);
}
