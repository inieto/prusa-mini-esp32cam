#include "device/camera.hpp"

#include <driver/ledc.h>
#include <esp_camera.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>

#include "device/board.hpp"

namespace device {
namespace {

constexpr const char* kTag = "camera";
constexpr ledc_timer_t kLedTimer = LEDC_TIMER_1;      // TIMER_0/CHANNEL_0 drive XCLK
constexpr ledc_channel_t kLedChannel = LEDC_CHANNEL_1;
// 10-bit PWM at 5 kHz: fine steps at the low end of the 1..100 % intensity slider.
constexpr uint32_t kLedMaxDuty = (1u << 10) - 1;

framesize_t to_framesize(core::Resolution r) {
  switch (r) {
    case core::Resolution::QVGA: return FRAMESIZE_QVGA;
    case core::Resolution::CIF: return FRAMESIZE_CIF;
    case core::Resolution::VGA: return FRAMESIZE_VGA;
    case core::Resolution::SVGA: return FRAMESIZE_SVGA;
    case core::Resolution::XGA: return FRAMESIZE_XGA;
    case core::Resolution::SXGA: return FRAMESIZE_SXGA;
    case core::Resolution::UXGA: return FRAMESIZE_UXGA;
    case core::Resolution::QXGA: return FRAMESIZE_QXGA;
  }
  return FRAMESIZE_SVGA;
}

}  // namespace

EspCamera::EspCamera(const core::WallClock& wall, const core::Clock& clock)
    : wall_(wall), clock_(clock) {}

void EspCamera::detect_sensor() {
  sensor_t* sensor = esp_camera_sensor_get();
  if (!sensor) return;
  const camera_sensor_info_t* info = esp_camera_sensor_get_info(&sensor->id);
  if (info) {
    sensor_name_ = info->name;
    sensor_size_ = {resolution[info->max_size].width, resolution[info->max_size].height};
  }
  sensor_pid_ = sensor->id.PID;
  // Larger sensors (OV5640) could go beyond QXGA, but PSRAM cannot hold bigger frames
  // three times over next to the driver's own buffers.
  max_resolution_ = info && info->max_size >= FRAMESIZE_QXGA ? core::Resolution::QXGA
                                                             : core::Resolution::UXGA;
  ESP_LOGI(kTag, "sensor %s (PID 0x%04x), up to %ux%u", sensor_name_.c_str(),
           static_cast<unsigned>(sensor->id.PID), core::size_of(max_resolution_).width,
           core::size_of(max_resolution_).height);
}

core::Result<void> EspCamera::init(const core::CameraSettings& settings) {
  const ledc_timer_config_t timer = {
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .duty_resolution = LEDC_TIMER_10_BIT,
      .timer_num = kLedTimer,
      .freq_hz = 5000,
      .clk_cfg = LEDC_AUTO_CLK,
      .deconfigure = false,
  };
  ledc_timer_config(&timer);
  const ledc_channel_config_t channel = {
      .gpio_num = board::kFlashLed,
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .channel = kLedChannel,
      .intr_type = LEDC_INTR_DISABLE,
      .timer_sel = kLedTimer,
      .duty = 0,
      .hpoint = 0,
      .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
      .flags = {},
  };
  ledc_channel_config(&channel);

  // Every supported sensor handles UXGA, so start there at most; only once the sensor is known
  // can a larger resolution be requested (or a stored one be clamped after a sensor swap).
  core::CameraSettings s = settings;
  const core::Resolution first = std::min(s.resolution, core::Resolution::UXGA);
  if (auto r = start_driver(first, s.jpeg_quality); !r) return r;
  detect_sensor();
  if (!supports(s.resolution)) s.resolution = max_resolution_;
  if (s.resolution != first) {
    if (auto r = start_driver(s.resolution, s.jpeg_quality); !r) return r;
  }
  apply_framing(s);
  apply_sensor(s);
  std::lock_guard lock(mutex_);
  current_ = s;
  return {};
}

bool EspCamera::supports_sharpness() const {
  return sensor_pid_ == OV3660_PID || sensor_pid_ == OV5640_PID;
}

uint16_t EspCamera::max_zoom_pct(core::Resolution r) const {
  if (sensor_pid_ != OV3660_PID && sensor_pid_ != OV2640_PID) return 100;
  return core::max_zoom_pct(sensor_size_, core::size_of(r));
}

// Digital zoom through the sensor's own windowing (ADR-0015): it reads only the chosen region of
// the pixel array and scales it to the output size, so a zoomed frame costs nothing extra and
// keeps real detail. Register meanings differ per sensor; values mirror what esp32-camera
// programs for the standard frame sizes.
void EspCamera::apply_framing(const core::CameraSettings& s) {
  sensor_t* sensor = esp_camera_sensor_get();
  if (!sensor) return;
  const core::Size out = core::size_of(s.resolution);
  core::CameraSettings framing = s;
  framing.zoom_pct = std::min(s.zoom_pct, max_zoom_pct(s.resolution));
  const core::SensorWindow w = core::framing_window(sensor_size_, out, framing);
  if (applied_window_ && *applied_window_ == w && applied_resolution_ == s.resolution) return;

  const bool unzoomed = framing.zoom_pct <= 100;
  int err = 0;
  if (unzoomed) {
    // The driver's own setting for this frame size (also undoes a previous zoom).
    if (applied_window_) err = sensor->set_framesize(sensor, to_framesize(s.resolution));
  } else if (sensor_pid_ == OV3660_PID) {
    // Array is 2048x1536 plus a 32x12 border: full frame is x 0..2079, y 0..1547.
    const bool binning = out.width <= w.width / 2 && out.height <= w.height / 2;
    const bool scale = !((out.width == w.width && out.height == w.height) ||
                         (out.width == w.width / 2 && out.height == w.height / 2));
    err = sensor->set_res_raw(sensor, w.x, w.y, w.x + w.width + 31, w.y + w.height + 11,
                              binning ? 8 : 16, binning ? 2 : 6, 2300, binning ? 1564 / 2 + 1 : 1564,
                              out.width, out.height, scale, binning);
  } else if (sensor_pid_ == OV2640_PID) {
    // Full-resolution (UXGA, mode 0) readout; the DSP crops the window and scales it.
    err = sensor->set_res_raw(sensor, 0, 0, 0, 0, w.x, w.y, w.width, w.height, out.width,
                              out.height, false, false);
  }
  if (err) {
    ESP_LOGW(kTag, "framing rejected by the sensor (%d)", err);
    return;
  }
  applied_window_ = w;
  applied_resolution_ = s.resolution;
  if (!unzoomed) {
    ESP_LOGI(kTag, "framing: zoom %u%%, sensor window %ux%u at %u,%u", framing.zoom_pct, w.width,
             w.height, w.x, w.y);
  }
}

core::Result<void> EspCamera::start_driver(core::Resolution resolution, uint8_t quality) {
  if (driver_started_) {
    esp_camera_deinit();
    driver_started_ = false;
  }
  camera_config_t config = {};
  config.pin_pwdn = board::kCamPwdn;
  config.pin_reset = board::kCamReset;
  config.pin_xclk = board::kCamXclk;
  config.pin_sccb_sda = board::kCamSda;
  config.pin_sccb_scl = board::kCamScl;
  config.pin_d7 = board::kCamD7;
  config.pin_d6 = board::kCamD6;
  config.pin_d5 = board::kCamD5;
  config.pin_d4 = board::kCamD4;
  config.pin_d3 = board::kCamD3;
  config.pin_d2 = board::kCamD2;
  config.pin_d1 = board::kCamD1;
  config.pin_d0 = board::kCamD0;
  config.pin_vsync = board::kCamVsync;
  config.pin_href = board::kCamHref;
  config.pin_pclk = board::kCamPclk;
  config.xclk_freq_hz = board::kCamXclkHz;
  config.ledc_timer = LEDC_TIMER_0;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = to_framesize(resolution);
  config.jpeg_quality = quality;
  config.jpeg_buffer_size = core::driver_jpeg_buffer(resolution);
  // Two buffers keep the latest frame ready; at QXGA only one fits in PSRAM, and capture()
  // then drops the frame that waited in it (taken whenever the previous one was returned).
  fb_count_ = resolution > core::Resolution::UXGA ? 1 : 2;
  config.fb_count = fb_count_;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.grab_mode = fb_count_ == 1 ? CAMERA_GRAB_WHEN_EMPTY : CAMERA_GRAB_LATEST;

  const esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "esp_camera_init failed: %s", esp_err_to_name(err));
    return core::fail(core::Errc::Io, err);
  }
  driver_started_ = true;
  applied_window_.reset();  // a fresh driver frames the whole sensor again
  return {};
}

void EspCamera::apply_sensor(const core::CameraSettings& s) {
  sensor_t* sensor = esp_camera_sensor_get();
  if (!sensor) return;
  sensor->set_quality(sensor, s.jpeg_quality);
  sensor_quality_ = s.jpeg_quality;
  sensor->set_brightness(sensor, s.brightness);
  sensor->set_contrast(sensor, s.contrast);
  sensor->set_saturation(sensor, s.saturation);
  sensor->set_exposure_ctrl(sensor, s.auto_exposure);
  sensor->set_aec2(sensor, s.aec_dsp);
  sensor->set_ae_level(sensor, s.ae_level);
  sensor->set_aec_value(sensor, s.manual_exposure);
  sensor->set_gain_ctrl(sensor, s.auto_gain);
  sensor->set_agc_gain(sensor, s.manual_gain);
  sensor->set_gainceiling(sensor, static_cast<gainceiling_t>(s.gain_ceiling));
  sensor->set_whitebal(sensor, s.auto_white_balance);
  sensor->set_awb_gain(sensor, s.auto_white_balance);
  sensor->set_wb_mode(sensor, s.wb_mode);
  if (supports_sharpness()) {
    sensor->set_sharpness(sensor, s.sharpness);
    sensor->set_denoise(sensor, s.denoise);
  }
  sensor->set_hmirror(sensor, s.hmirror);
  sensor->set_vflip(sensor, s.vflip);
  sensor->set_lenc(sensor, s.lens_correction);
  sensor->set_bpc(sensor, 1);
  sensor->set_wpc(sensor, 1);
  sensor->set_raw_gma(sensor, 1);
  sensor->set_dcw(sensor, 1);
}

core::Result<void> EspCamera::apply(const core::CameraSettings& settings) {
  if (auto v = core::validate(settings); !v) return v;
  if (!supports(settings.resolution)) return core::fail(core::Errc::OutOfRange);
  const core::CameraSettings previous = this->settings();
  if (settings.resolution != previous.resolution) {
    // The driver sizes its buffers at init, so a resolution change needs a restart.
    if (auto r = start_driver(settings.resolution, settings.jpeg_quality); !r) return r;
  }
  apply_framing(settings);
  apply_sensor(settings);
  std::lock_guard lock(mutex_);
  current_ = settings;
  return {};
}

core::CameraSettings EspCamera::settings() const {
  std::lock_guard lock(mutex_);
  return pending_.value_or(current_);
}

void EspCamera::request_settings(const core::CameraSettings& settings) {
  std::lock_guard lock(mutex_);
  pending_ = settings;
}

void EspCamera::set_led_duty(uint8_t percent) {
  // Round up so 1 % always lights the LED a little.
  const uint32_t duty = percent == 0 ? 0 : std::max<uint32_t>(1, (kLedMaxDuty * percent + 99) / 100);
  ledc_set_duty(LEDC_LOW_SPEED_MODE, kLedChannel, duty);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, kLedChannel);
}

void EspCamera::set_light(bool on) {
  // Pending settings count: a new intensity applies before the capture task picks it up.
  const uint8_t duty = settings().flash_duty_pct;
  light_on_ = on;
  set_led_duty(on ? duty : 0);
}

core::Result<core::FrameRef> EspCamera::capture(core::CaptureKind kind) {
  std::optional<core::CameraSettings> pending;
  {
    std::lock_guard lock(mutex_);
    pending.swap(pending_);
  }
  if (pending) {
    if (auto r = apply(*pending); !r) ESP_LOGW(kTag, "settings rejected (%d)", (int)r.error().code);
  }
  const core::CameraSettings s = settings();
  if (!driver_started_ || !pool_) return core::fail(core::Errc::Io);

  std::optional<core::FrameWriter> writer = pool_->acquire();
  if (!writer) return core::fail(core::Errc::Busy);  // every slot is being read

  const bool flash = s.flash_on_capture && !light_on_ && kind == core::CaptureKind::Snapshot;
  if (flash) {
    set_led_duty(s.flash_duty_pct);
    vTaskDelay(pdMS_TO_TICKS(s.flash_lead_ms));
  }
  // Snapshots and live frames use different JPEG quality. The register applies from the next
  // frame on, so after a switch the frames already queued in the driver are dropped.
  const uint8_t quality = kind == core::CaptureKind::Preview ? s.live_jpeg_quality : s.jpeg_quality;
  int stale_frames = flash || fb_count_ == 1 ? 1 : 0;
  if (quality != sensor_quality_) {
    if (sensor_t* sensor = esp_camera_sensor_get()) sensor->set_quality(sensor, quality);
    sensor_quality_ = quality;
    stale_frames = fb_count_;
  }
  for (int i = 0; i < stale_frames; ++i) {
    // Drop frames exposed before the LED was on, with the old quality, or waiting in the buffer.
    if (camera_fb_t* stale = esp_camera_fb_get()) esp_camera_fb_return(stale);
  }

  camera_fb_t* fb = esp_camera_fb_get();
  if (flash) set_led_duty(0);  // always, whatever happened with the capture
  if (!fb) return core::fail(core::Errc::Timeout);

  const core::ExifInfo exif{.rotation = s.rotation, .model = sensor_name_,
                            .taken_at = wall_.utc_now()};
  const core::Result<size_t> written =
      core::write_jpeg_with_exif({fb->buf, fb->len}, exif, writer->buffer());
  const core::FrameInfo info{.width = static_cast<uint16_t>(fb->width),
                             .height = static_cast<uint16_t>(fb->height),
                             .captured_at = clock_.now(),
                             .sequence = ++sequence_};
  esp_camera_fb_return(fb);  // the driver gets its buffer back immediately (ADR-0003)

  if (!written) return std::unexpected(written.error());
  return std::move(*writer).commit(*written, info);
}

}  // namespace device
