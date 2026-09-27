// Minimal EXIF (APP1) writer so browsers and PrusaConnect honour the configured rotation.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "core/error.hpp"
#include "core/time.hpp"

namespace core {

enum class Rotation : uint8_t { Deg0, Deg90, Deg180, Deg270 };

struct ExifInfo {
  Rotation rotation = Rotation::Deg0;
  std::string_view make = "OmniVision";
  std::string_view model = "OV2640";
  std::string_view software = "PrusaCam byClaude";
  std::optional<CivilTime> taken_at;
};

// EXIF orientation tag value for a clockwise rotation.
uint16_t exif_orientation(Rotation rotation);

// Copies `jpeg` into `out` replacing any APP0/APP1 after SOI with a fresh EXIF APP1.
// Returns the number of bytes written.
Result<size_t> write_jpeg_with_exif(std::span<const uint8_t> jpeg, const ExifInfo& info,
                                    std::span<uint8_t> out);

}  // namespace core
