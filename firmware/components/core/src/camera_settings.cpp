#include "core/camera_settings.hpp"

#include <array>

namespace core {
namespace {

constexpr std::array kResolutions = {
    Resolution::QVGA, Resolution::CIF,  Resolution::VGA,  Resolution::SVGA,
    Resolution::XGA,  Resolution::SXGA, Resolution::UXGA,
};

constexpr std::array<Size, 7> kSizes = {{
    {320, 240}, {352, 288}, {640, 480}, {800, 600}, {1024, 768}, {1280, 1024}, {1600, 1200},
}};

bool in_range(int value, int lo, int hi) { return value >= lo && value <= hi; }

}  // namespace

Size size_of(Resolution resolution) { return kSizes[static_cast<size_t>(resolution)]; }

std::optional<Resolution> resolution_for(Size size) {
  for (Resolution r : kResolutions) {
    if (size_of(r) == size) return r;
  }
  return std::nullopt;
}

std::span<const Resolution> all_resolutions() { return kResolutions; }

size_t max_jpeg_size(Resolution resolution, uint8_t jpeg_quality) {
  const Size s = size_of(resolution);
  const size_t pixels = size_t{s.width} * s.height;
  // Empirical OV2640 bounds: a busy scene at quality 10 stays under ~0.25 B/pixel.
  const size_t divisor = jpeg_quality <= 12 ? 4 : jpeg_quality <= 20 ? 6 : 10;
  return pixels / divisor + 16 * 1024;  // headroom for EXIF and headers
}

Result<void> validate(const CameraSettings& s) {
  const bool ok = in_range(static_cast<int>(s.resolution), 0, kResolutions.size() - 1) &&
                  in_range(s.jpeg_quality, 10, 63) && in_range(s.brightness, -2, 2) &&
                  in_range(s.contrast, -2, 2) && in_range(s.saturation, -2, 2) &&
                  in_range(s.ae_level, -2, 2) && in_range(s.manual_exposure, 0, 1200) &&
                  in_range(s.gain_ceiling, 0, 6) && in_range(s.manual_gain, 0, 30) &&
                  in_range(static_cast<int>(s.rotation), 0, 3) &&
                  in_range(s.flash_lead_ms, 0, 2000) && in_range(s.flash_duty_pct, 0, 100);
  if (!ok) return fail(Errc::OutOfRange);
  return {};
}

}  // namespace core
