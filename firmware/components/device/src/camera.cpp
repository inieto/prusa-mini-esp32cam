#include "device/camera.hpp"

#include <driver/ledc.h>
#include <esp_camera.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "device/board.hpp"

namespace device {
namespace {

constexpr const char* kTag = "camera";
constexpr ledc_timer_t kLedTimer = LEDC_TIMER_1;      // TIMER_0/CHANNEL_0 drive XCLK
constexpr ledc_channel_t kLedChannel = LEDC_CHANNEL_1;
constexpr uint32_t kLedMaxDuty = (1u << 8) - 1;

framesize_t to_framesize(core::Resolution r) {
  switch (r) {
    case core::Resolution::QVGA: return FRAMESIZE_QVGA;
    case core::Resolution::CIF: return FRAMESIZE_CIF;
    case core::Resolution::VGA: return FRAMESIZE_VGA;
    case core::Resolution::SVGA: return FRAMESIZE_SVGA;
    case core::Resolution::XGA: return FRAMESIZE_XGA;
    case core::Resolution::SXGA: return FRAMESIZE_SXGA;
    case core::Resolution::UXGA: return FRAMESIZE_UXGA;
  }
  return FRAMESIZE_SVGA;
}

}  // namespace

Ov2640Camera::Ov2640Camera(core::FramePool& pool, const core::WallClock& wall,
                           const core::Clock& clock)
    : pool_(pool), wall_(wall), clock_(clock) {}

core::Result<void> Ov2640Camera::init(const core::CameraSettings& settings) {
  const ledc_timer_config_t timer = {
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .duty_resolution = LEDC_TIMER_8_BIT,
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

  if (auto r = start_driver(settings.resolution, settings.jpeg_quality); !r) return r;
  apply_sensor(settings);
  std::lock_guard lock(mutex_);
  current_ = settings;
  return {};
}

core::Result<void> Ov2640Camera::start_driver(core::Resolution resolution, uint8_t quality) {
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
  config.fb_count = 2;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.grab_mode = CAMERA_GRAB_LATEST;  // with 2 buffers the frame is never stale

  const esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "esp_camera_init failed: %s", esp_err_to_name(err));
    return core::fail(core::Errc::Io, err);
  }
  driver_started_ = true;
  return {};
}

void Ov2640Camera::apply_sensor(const core::CameraSettings& s) {
  sensor_t* sensor = esp_camera_sensor_get();
  if (!sensor) return;
  sensor->set_quality(sensor, s.jpeg_quality);
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
  sensor->set_hmirror(sensor, s.hmirror);
  sensor->set_vflip(sensor, s.vflip);
  sensor->set_lenc(sensor, s.lens_correction);
  sensor->set_bpc(sensor, 1);
  sensor->set_wpc(sensor, 1);
  sensor->set_raw_gma(sensor, 1);
  sensor->set_dcw(sensor, 1);
}

core::Result<void> Ov2640Camera::apply(const core::CameraSettings& settings) {
  if (auto v = core::validate(settings); !v) return v;
  const core::CameraSettings previous = this->settings();
  if (settings.resolution != previous.resolution) {
    // The driver sizes its buffers at init, so a resolution change needs a restart.
    if (auto r = start_driver(settings.resolution, settings.jpeg_quality); !r) return r;
  }
  apply_sensor(settings);
  std::lock_guard lock(mutex_);
  current_ = settings;
  return {};
}

core::CameraSettings Ov2640Camera::settings() const {
  std::lock_guard lock(mutex_);
  return pending_.value_or(current_);
}

void Ov2640Camera::request_settings(const core::CameraSettings& settings) {
  std::lock_guard lock(mutex_);
  pending_ = settings;
}

void Ov2640Camera::set_led_duty(uint8_t percent) {
  ledc_set_duty(LEDC_LOW_SPEED_MODE, kLedChannel, kLedMaxDuty * percent / 100);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, kLedChannel);
}

void Ov2640Camera::set_light(bool on) {
  uint8_t duty;
  {
    std::lock_guard lock(mutex_);
    duty = current_.flash_duty_pct;
  }
  light_on_ = on;
  set_led_duty(on ? duty : 0);
}

core::Result<core::FrameRef> Ov2640Camera::capture() {
  std::optional<core::CameraSettings> pending;
  {
    std::lock_guard lock(mutex_);
    pending.swap(pending_);
  }
  if (pending) {
    if (auto r = apply(*pending); !r) ESP_LOGW(kTag, "settings rejected (%d)", (int)r.error().code);
  }
  const core::CameraSettings s = settings();
  if (!driver_started_) return core::fail(core::Errc::Io);

  std::optional<core::FrameWriter> writer = pool_.acquire();
  if (!writer) return core::fail(core::Errc::Busy);  // every slot is being read

  const bool flash = s.flash_on_capture && !light_on_;
  if (flash) {
    set_led_duty(s.flash_duty_pct);
    vTaskDelay(pdMS_TO_TICKS(s.flash_lead_ms));
    // Drop the frame exposed before the LED was on.
    if (camera_fb_t* stale = esp_camera_fb_get()) esp_camera_fb_return(stale);
  }

  camera_fb_t* fb = esp_camera_fb_get();
  if (flash) set_led_duty(0);  // always, whatever happened with the capture
  if (!fb) return core::fail(core::Errc::Timeout);

  const core::ExifInfo exif{.rotation = s.rotation, .taken_at = wall_.utc_now()};
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
