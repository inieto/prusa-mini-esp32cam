// MJPEG live stream (ADR-0014). The HTTP handler hands the request to this task with
// httpd_req_async_handler_begin, so the web UI keeps working while a stream is open. One viewer
// at a time: frames come from FramePool slots shared with uploads and the timelapse.
#pragma once

#include <esp_http_server.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>

#include "core/snapshot_service.hpp"

namespace device {

class LiveStream {
 public:
  explicit LiveStream(core::SnapshotService& service) : service_(service) {}

  void start();
  // From the (authenticated) HTTP handler. Returns once the stream task owns the request.
  esp_err_t serve(httpd_req_t* req);
  bool watching() const { return active_; }
  // Read by the capture task: the stream is ready for its next frame. Frames are captured on
  // demand, so the rate follows what the link can carry instead of a fixed 5 fps.
  bool wants_frame() const { return active_ && wants_frame_; }

 private:
  static void task_entry(void* self);
  void run();
  void stream(httpd_req_t* req);

  core::SnapshotService& service_;
  TaskHandle_t task_ = nullptr;
  std::atomic<bool> active_{false};
  std::atomic<bool> wants_frame_{false};
  std::atomic<httpd_req_t*> request_{nullptr};
};

}  // namespace device
