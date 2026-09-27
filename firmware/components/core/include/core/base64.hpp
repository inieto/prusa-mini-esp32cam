#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace core {

std::string base64_encode(std::span<const uint8_t> data);

}  // namespace core
