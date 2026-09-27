#include "core/base64.hpp"

namespace core {

std::string base64_encode(std::span<const uint8_t> data) {
  static constexpr char kAlphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((data.size() + 2) / 3 * 4);
  size_t i = 0;
  for (; i + 3 <= data.size(); i += 3) {
    const uint32_t v = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
    out += kAlphabet[(v >> 18) & 0x3F];
    out += kAlphabet[(v >> 12) & 0x3F];
    out += kAlphabet[(v >> 6) & 0x3F];
    out += kAlphabet[v & 0x3F];
  }
  const size_t rest = data.size() - i;
  if (rest > 0) {
    uint32_t v = data[i] << 16;
    if (rest == 2) v |= data[i + 1] << 8;
    out += kAlphabet[(v >> 18) & 0x3F];
    out += kAlphabet[(v >> 12) & 0x3F];
    out += rest == 2 ? kAlphabet[(v >> 6) & 0x3F] : '=';
    out += '=';
  }
  return out;
}

}  // namespace core
