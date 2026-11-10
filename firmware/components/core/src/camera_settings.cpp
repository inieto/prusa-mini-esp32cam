#include "core/camera_settings.hpp"

#include <algorithm>
#include <array>

namespace core {
namespace {

constexpr std::array kResolutions = {
    Resolution::QVGA, Resolution::CIF,  Resolution::VGA,  Resolution::SVGA,
    Resolution::XGA,  Resolution::SXGA, Resolution::UXGA, Resolution::QXGA,
};

constexpr std::array<Size, 8> kSizes = {{
    {320, 240}, {352, 288}, {640, 480}, {800, 600}, {1024, 768}, {1280, 1024}, {1600, 1200},
    {2048, 1536},
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

size_t driver_jpeg_buffer(Resolution resolution) {
  const Size s = size_of(resolution);
  const size_t pixels = size_t{s.width} * s.height;
  return pixels / (resolution > Resolution::UXGA ? 4 : 3);
}

size_t max_jpeg_size(Resolution resolution) {
  return driver_jpeg_buffer(resolution) + 16 * 1024;  // headroom for EXIF and headers
}

Result<void> validate(const CameraSettings& s) {
  const bool ok = in_range(static_cast<int>(s.resolution), 0, kResolutions.size() - 1) &&
                  in_range(s.jpeg_quality, 10, 63) && in_range(s.brightness, -2, 2) &&
                  in_range(s.contrast, -2, 2) && in_range(s.saturation, -2, 2) &&
                  in_range(s.ae_level, -2, 2) && in_range(s.manual_exposure, 0, 1200) &&
                  in_range(s.gain_ceiling, 0, 6) && in_range(s.manual_gain, 0, 30) &&
                  in_range(static_cast<int>(s.rotation), 0, 3) &&
                  in_range(s.flash_lead_ms, 0, 2000) && in_range(s.flash_duty_pct, 0, 100) &&
                  in_range(s.zoom_pct, 100, 400) && in_range(s.pan_x, -100, 100) &&
                  in_range(s.pan_y, -100, 100);
  if (!ok) return fail(Errc::OutOfRange);
  return {};
}

uint16_t max_zoom_pct(Size sensor, Size output) {
  if (output.width == 0 || output.height == 0) return 100;
  const uint32_t zx = 100u * sensor.width / output.width;
  const uint32_t zy = 100u * sensor.height / output.height;
  return static_cast<uint16_t>(std::clamp<uint32_t>(std::min(zx, zy), 100, 400));
}

SensorWindow framing_window(Size sensor, Size output, const CameraSettings& s, uint16_t align) {
  const auto down = [align](uint32_t v) { return static_cast<uint16_t>(v / align * align); };

  // Largest window with the output's aspect ratio that fits the sensor.
  uint32_t base_w = sensor.width;
  uint32_t base_h = uint32_t{sensor.width} * output.height / output.width;
  if (base_h > sensor.height) {
    base_h = sensor.height;
    base_w = uint32_t{sensor.height} * output.width / output.height;
  }
  const uint32_t zoom = std::clamp<uint32_t>(s.zoom_pct, 100, max_zoom_pct(sensor, output));
  const uint16_t w = down(base_w * 100 / zoom);
  const uint16_t h = down(base_h * 100 / zoom);

  // Visual pan → sensor pan: undo the viewer's clockwise rotation, then the sensor's mirroring.
  int vx = s.pan_x, vy = s.pan_y, dx = vx, dy = vy;
  switch (s.rotation) {
    case Rotation::Deg0: break;
    case Rotation::Deg90: dx = vy; dy = -vx; break;
    case Rotation::Deg180: dx = -vx; dy = -vy; break;
    case Rotation::Deg270: dx = -vy; dy = vx; break;
  }
  if (s.hmirror) dx = -dx;
  if (s.vflip) dy = -dy;

  const int free_x = sensor.width - w, free_y = sensor.height - h;
  const auto place = [&](int free, int pan) {
    return down(static_cast<uint32_t>(std::clamp(free / 2 + free * pan / 200, 0, free)));
  };
  return {place(free_x, dx), place(free_y, dy), w, h};
}

}  // namespace core
