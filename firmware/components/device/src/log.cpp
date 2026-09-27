#include "device/log.hpp"

#include <esp_log.h>

namespace device {

void EspLog::write(core::LogLevel level, std::string_view m) {
  constexpr const char* kTag = "core";
  const int n = static_cast<int>(m.size());
  switch (level) {
    case core::LogLevel::Error: ESP_LOGE(kTag, "%.*s", n, m.data()); break;
    case core::LogLevel::Warn: ESP_LOGW(kTag, "%.*s", n, m.data()); break;
    case core::LogLevel::Info: ESP_LOGI(kTag, "%.*s", n, m.data()); break;
    case core::LogLevel::Debug: ESP_LOGD(kTag, "%.*s", n, m.data()); break;
  }
}

}  // namespace device
