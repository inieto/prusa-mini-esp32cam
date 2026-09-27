#pragma once

#include "core/ports.hpp"

namespace device {

// Core log messages go through ESP_LOG (serial). The RAM ring buffer for the web UI and the
// batched SD sink arrive with the web slice (ARCHITECTURE §8).
struct EspLog final : core::Log {
  void write(core::LogLevel level, std::string_view message) override;
};

}  // namespace device
