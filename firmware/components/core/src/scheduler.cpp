#include "core/scheduler.hpp"

namespace core {

SnapshotScheduler::SnapshotScheduler(std::optional<Millis> interval)
    : interval_(interval),
      next_at_(interval ? std::optional<Millis>(Millis{0}) : std::nullopt) {}

void SnapshotScheduler::set_interval(std::optional<Millis> interval, Millis now) {
  interval_ = interval;
  if (!interval) {
    next_at_.reset();
    return;
  }
  // Shortening the interval takes effect now; lengthening waits for the current slot.
  if (!next_at_ || *next_at_ > now + *interval) next_at_ = now + *interval;
}

bool SnapshotScheduler::due(Millis now) const {
  return forced_ || (next_at_ && now >= *next_at_);
}

void SnapshotScheduler::schedule_after(Millis now, std::optional<Millis> delay) {
  forced_ = false;
  if (delay) {
    next_at_ = now + *delay;
  } else if (interval_) {
    next_at_ = now + *interval_;
  } else {
    next_at_.reset();
  }
}

}  // namespace core
