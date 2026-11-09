#include "core/mqtt_discovery.hpp"

#include <cJSON.h>
#include <gtest/gtest.h>

using namespace core;

namespace {
mqtt::Identity identity() {
  return {.node_id = mqtt::node_id_from_mac("F8:B3:B7:A8:45:84"),
          .name = "prusacam",
          .base = "prusacam/prusacam",
          .firmware = "0.2.0"};
}
}  // namespace

TEST(MqttDiscovery, NodeIdIsTopicSafe) {
  EXPECT_EQ(mqtt::node_id_from_mac("F8:B3:B7:A8:45:84"), "prusacam_f8b3b7a84584");
}

TEST(MqttDiscovery, PublishesRetainedConfigsUnderTheDiscoveryPrefix) {
  const auto messages = mqtt::discovery_messages(identity());
  ASSERT_GE(messages.size(), 6u);
  bool camera = false, light = false;
  for (const auto& m : messages) {
    EXPECT_TRUE(m.retain);
    EXPECT_TRUE(m.topic.starts_with("homeassistant/")) << m.topic;
    EXPECT_TRUE(m.topic.ends_with("/config")) << m.topic;
    cJSON* j = cJSON_Parse(m.payload.c_str());
    ASSERT_NE(j, nullptr) << m.payload;
    EXPECT_STREQ(cJSON_GetObjectItem(j, "availability_topic")->valuestring,
                 "prusacam/prusacam/availability");
    const cJSON* ids = cJSON_GetObjectItem(cJSON_GetObjectItem(j, "device"), "identifiers");
    EXPECT_STREQ(cJSON_GetArrayItem(ids, 0)->valuestring, "prusacam_f8b3b7a84584");
    if (m.topic == "homeassistant/camera/prusacam_f8b3b7a84584/camera/config") {
      camera = true;
      EXPECT_STREQ(cJSON_GetObjectItem(j, "topic")->valuestring, "prusacam/prusacam/image");
    }
    if (m.topic == "homeassistant/switch/prusacam_f8b3b7a84584/light/config") {
      light = true;
      EXPECT_STREQ(cJSON_GetObjectItem(j, "command_topic")->valuestring,
                   "prusacam/prusacam/light/set");
    }
    cJSON_Delete(j);
  }
  EXPECT_TRUE(camera);
  EXPECT_TRUE(light);
}

TEST(MqttDiscovery, StatePayloadCarriesPrinterAndCounters) {
  mqtt::State s{.connect = "online", .light = true, .rssi = -61, .uploads_ok = 12,
                .printer = {.phase = PrinterPhase::Printing, .progress = 45.6f},
                .printer_known = true};
  cJSON* j = cJSON_Parse(mqtt::state_payload(s).c_str());
  ASSERT_NE(j, nullptr);
  EXPECT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(j, "light")));
  EXPECT_EQ(cJSON_GetObjectItem(j, "rssi")->valueint, -61);
  const cJSON* p = cJSON_GetObjectItem(j, "printer");
  EXPECT_STREQ(cJSON_GetObjectItem(p, "state")->valuestring, "printing");
  EXPECT_EQ(cJSON_GetObjectItem(p, "progress")->valueint, 45);
  cJSON_Delete(j);
}
