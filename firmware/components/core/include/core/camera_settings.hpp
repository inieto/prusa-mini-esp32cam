// Sensor-independent camera settings with their valid ranges.
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "core/error.hpp"
#include "core/exif.hpp"

namespace core {

// Appended only: the numeric value is stored in NVS. QXGA needs an OV3660/OV5640 sensor.
enum class Resolution : uint8_t { QVGA, CIF, VGA, SVGA, XGA, SXGA, UXGA, QXGA };

struct Size {
  uint16_t width, height;
  friend bool operator==(Size, Size) = default;
};

Size size_of(Resolution resolution);
std::optional<Resolution> resolution_for(Size size);
std::span<const Resolution> all_resolutions();

struct CameraSettings {
  Resolution resolution = Resolution::SVGA;
  uint8_t jpeg_quality = 12;  // 10 (best) .. 63 (worst), OV2640 driver scale
  int8_t brightness = 0;      // -2..2
  int8_t contrast = 0;        // -2..2
  int8_t saturation = 0;      // -2..2
  bool auto_exposure = true;
  bool aec_dsp = true;        // "aec2": night-friendly exposure in the DSP
  int8_t ae_level = 0;        // -2..2
  uint16_t manual_exposure = 300;  // 0..1200, used when auto_exposure is off
  bool auto_gain = true;
  uint8_t gain_ceiling = 4;   // 0..6 → 2x,4x,8x,16x,32x,64x,128x (v1.1.2 hardcoded 2x, F2)
  uint8_t manual_gain = 0;    // 0..30, used when auto_gain is off
  bool auto_white_balance = true;
  bool hmirror = false;
  bool vflip = false;
  bool lens_correction = true;
  Rotation rotation = Rotation::Deg0;
  bool flash_on_capture = false;
  uint16_t flash_lead_ms = 200;  // 0..2000: light up before exposure
  uint8_t flash_duty_pct = 80;   // 0..100 (LED is overdriven at 100 %)
  friend bool operator==(const CameraSettings&, const CameraSettings&) = default;
};

// JPEG buffer we ask the camera driver for. Its default (width*height/5) overflows with the
// OV3660 at good quality ("FB-OVF", frame dropped): 1/3 byte per pixel up to UXGA, 1/4 at QXGA
// where PSRAM is tight. A JPEG larger than this cannot reach us.
size_t driver_jpeg_buffer(Resolution resolution);

// FramePool slot size: the driver buffer plus headroom for the EXIF block we prepend.
size_t max_jpeg_size(Resolution resolution);

Result<void> validate(const CameraSettings& settings);

}  // namespace core
