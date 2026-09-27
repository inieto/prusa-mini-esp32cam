// Read-only view over the web UI bundle embedded in flash (built by tools/pack_web.py).
// Format (little endian): "PCWB" u8 version=1, char etag[16], u8 count,
// then per asset: u8 path_len, path, u8 type_len, content_type, u32 size, gzip bytes.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "core/error.hpp"

namespace core {

struct WebAsset {
  std::string_view path;  // without leading slash, e.g. "icons/logo.svg"
  std::string_view content_type;
  std::span<const uint8_t> gzip;
};

class WebBundle {
 public:
  static constexpr size_t kMaxAssets = 32;

  static Result<WebBundle> parse(std::span<const uint8_t> data);

  std::optional<WebAsset> find(std::string_view path) const;
  std::string_view etag() const { return etag_; }
  size_t size() const { return count_; }

 private:
  std::array<WebAsset, kMaxAssets> assets_{};
  size_t count_ = 0;
  std::string_view etag_;
};

}  // namespace core
