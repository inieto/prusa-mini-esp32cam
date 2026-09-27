// Test doubles for the core ports.
#pragma once

#include <deque>
#include <string>
#include <vector>

#include "core/ports.hpp"

namespace fakes {

using core::Millis;

struct ManualClock : core::Clock {
  Millis t{0};
  Millis now() const override { return t; }
  void advance(Millis d) { t += d; }
};

// Owns real buffers so FrameRefs behave exactly as on the device.
struct PoolStorage {
  std::vector<std::vector<uint8_t>> memory;
  std::vector<std::span<uint8_t>> spans;
  explicit PoolStorage(size_t slots, size_t size) : memory(slots, std::vector<uint8_t>(size)) {
    for (auto& m : memory) spans.emplace_back(m);
  }
};

struct FakeCamera : core::CameraPort {
  explicit FakeCamera(core::FramePool& pool) : pool(pool) {}
  core::FramePool& pool;
  core::CameraSettings current{};
  std::deque<core::Error> failures;  // consumed before producing frames
  uint32_t captures = 0;

  core::Result<core::FrameRef> capture() override {
    ++captures;
    if (!failures.empty()) {
      auto e = failures.front();
      failures.pop_front();
      return std::unexpected(e);
    }
    auto writer = pool.acquire();
    if (!writer) return core::fail(core::Errc::Busy);
    writer->buffer()[0] = 0xFF;
    return std::move(*writer).commit(1, {.sequence = captures});
  }
  core::Result<void> apply(const core::CameraSettings& s) override {
    current = s;
    return {};
  }
  core::CameraSettings settings() const override { return current; }
};

struct FakeConnect : core::ConnectApi {
  bool is_configured = true;
  std::deque<core::Error> upload_results;  // empty = success
  std::deque<core::Result<std::string>> info_results;
  std::vector<std::string> info_bodies;
  uint32_t uploads = 0;

  core::Result<void> upload_snapshot(const core::FrameRef&) override {
    ++uploads;
    if (upload_results.empty()) return {};
    auto e = upload_results.front();
    upload_results.pop_front();
    return std::unexpected(e);
  }
  core::Result<std::string> put_info(std::string_view json) override {
    info_bodies.emplace_back(json);
    if (info_results.empty()) return std::string("{}");
    auto r = info_results.front();
    info_results.pop_front();
    return r;
  }
  bool configured() const override { return is_configured; }
};

struct FakeNetwork : core::NetworkInfo {
  bool up = true;
  bool connected() const override { return up; }
  core::connect::NetworkSnapshot current() const override {
    return {.connected = up, .ipv4 = "192.168.0.61", .ssid = "home", .mac = "F8:B3:B7:A8:45:84"};
  }
};

struct FakeSystem : core::SystemControl {
  uint32_t reconnects = 0;
  std::vector<core::RebootReason> reboots;
  void reconnect_wifi() override { ++reconnects; }
  void reboot(core::RebootReason r) override { reboots.push_back(r); }
};

struct CapturingLog : core::Log {
  std::vector<std::string> lines;
  void write(core::LogLevel, std::string_view m) override { lines.emplace_back(m); }
};

}  // namespace fakes
