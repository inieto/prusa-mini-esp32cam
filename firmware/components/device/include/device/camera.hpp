// esp32-camera adapter (OV2640, OV3660, OV5640). Only the task running SnapshotService calls
// capture()/apply(); other tasks use request_settings(), applied before the next capture
// (single owner, ADR-0003).
#pragma once

#include <mutex>
#include <optional>
#include <string>

#include "core/frame_pool.hpp"
#include "core/ports.hpp"

namespace device {

class EspCamera final : public core::CameraPort {
 public:
  EspCamera(const core::WallClock& wall, const core::Clock& clock);

  // Starts the driver and detects the sensor. The frame pool is attached afterwards because its
  // slot size depends on the sensor's largest resolution.
  core::Result<void> init(const core::CameraSettings& settings);
  void attach_pool(core::FramePool& pool) { pool_ = &pool; }

  // Valid after init(): "OV2640", "OV3660", ... and the largest resolution we offer for it.
  const std::string& sensor_name() const { return sensor_name_; }
  core::Resolution max_resolution() const { return max_resolution_; }
  bool supports(core::Resolution r) const { return r <= max_resolution_; }

  core::Result<core::FrameRef> capture() override;
  core::Result<void> apply(const core::CameraSettings& settings) override;
  core::CameraSettings settings() const override;

  // Thread-safe: queue new settings for the owning task.
  void request_settings(const core::CameraSettings& settings);
  // Thread-safe: manual light (independent of flash-on-capture).
  void set_light(bool on);
  bool light() const { return light_on_; }

 private:
  core::Result<void> start_driver(core::Resolution resolution, uint8_t quality);
  void apply_sensor(const core::CameraSettings& settings);
  void set_led_duty(uint8_t percent);

  void detect_sensor();

  core::FramePool* pool_ = nullptr;
  const core::WallClock& wall_;
  const core::Clock& clock_;

  mutable std::mutex mutex_;  // guards pending_ and current_ copies
  core::CameraSettings current_{};
  std::optional<core::CameraSettings> pending_;
  bool driver_started_ = false;
  bool light_on_ = false;
  uint32_t sequence_ = 0;
  std::string sensor_name_ = "unknown";
  core::Resolution max_resolution_ = core::Resolution::UXGA;
};

}  // namespace device
