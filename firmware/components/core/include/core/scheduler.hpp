// Decides when the next snapshot is due. Pure: fed with monotonic time.
#pragma once

#include <optional>

#include "core/time.hpp"

namespace core {

class SnapshotScheduler {
 public:
  // An empty interval means manual mode: only trigger_now() schedules a capture.
  explicit SnapshotScheduler(std::optional<Millis> interval);

  void set_interval(std::optional<Millis> interval, Millis now);
  std::optional<Millis> interval() const { return interval_; }

  bool due(Millis now) const;
  void trigger_now() { forced_ = true; }

  // Called after every attempt. `delay` overrides the regular interval (e.g. backoff).
  void schedule_after(Millis now, std::optional<Millis> delay = std::nullopt);
  std::optional<Millis> next_at() const { return next_at_; }

 private:
  std::optional<Millis> interval_;
  std::optional<Millis> next_at_;  // empty = never (manual mode without trigger)
  bool forced_ = false;
};

}  // namespace core
