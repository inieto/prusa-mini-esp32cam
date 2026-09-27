// OV2640 adapter. Only the task running SnapshotService calls capture()/apply(); other tasks
// use request_settings(), applied before the next capture (single owner, ADR-0003).
#pragma once

#include <mutex>
#include <optional>

#include "core/frame_pool.hpp"
#include "core/ports.hpp"

namespace device {

class Ov2640Camera final : public core::CameraPort {
 public:
  Ov2640Camera(core::FramePool& pool, const core::WallClock& wall, const core::Clock& clock);

  core::Result<void> init(const core::CameraSettings& settings);

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

  core::FramePool& pool_;
  const core::WallClock& wall_;
  const core::Clock& clock_;

  mutable std::mutex mutex_;  // guards pending_ and current_ copies
  core::CameraSettings current_{};
  std::optional<core::CameraSettings> pending_;
  bool driver_started_ = false;
  bool light_on_ = false;
  uint32_t sequence_ = 0;
};

}  // namespace device
