#include "device/live_stream.hpp"

#include <esp_log.h>
#include <esp_timer.h>

#include <algorithm>
#include <cstdio>

namespace device {
namespace {

constexpr const char* kTag = "stream";
constexpr uint32_t kStack = 4096;
constexpr int64_t kMaxStreamUs = 30LL * 60 * 1'000'000;  // a forgotten tab stops after 30 min
constexpr TickType_t kPoll = pdMS_TO_TICKS(20);
constexpr int64_t kMinFramePeriodUs = 250'000;  // at most 4 fps
constexpr char kBoundary[] = "prusacamframe";

}  // namespace

void LiveStream::start() {
  xTaskCreatePinnedToCore(task_entry, "stream", kStack, this, 4, &task_, 1);
}

esp_err_t LiveStream::serve(httpd_req_t* req) {
  bool expected = false;
  if (!active_.compare_exchange_strong(expected, true)) {
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Retry-After", "10");
    return httpd_resp_sendstr(req, R"({"error":"another live view is open"})");
  }
  httpd_req_t* async = nullptr;
  if (httpd_req_async_handler_begin(req, &async) != ESP_OK) {
    active_ = false;
    return httpd_resp_send_500(req);
  }
  request_ = async;
  xTaskNotifyGive(task_);
  return ESP_OK;
}

void LiveStream::task_entry(void* self) { static_cast<LiveStream*>(self)->run(); }

void LiveStream::run() {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (httpd_req_t* req = request_.exchange(nullptr)) {
      ESP_LOGI(kTag, "live view started");
      stream(req);
      httpd_req_async_handler_complete(req);
      active_ = false;
      ESP_LOGI(kTag, "live view ended");
    }
  }
}

void LiveStream::stream(httpd_req_t* req) {
  httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=prusacamframe");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
  const int64_t started = esp_timer_get_time();
  uint32_t last_seq = service_.latest_frame() ? service_.latest_frame().info().sequence : 0;
  wants_frame_ = true;
  while (esp_timer_get_time() - started < kMaxStreamUs) {
    const core::FrameRef frame = service_.latest_frame();
    if (!frame || frame.info().sequence == last_seq) {
      vTaskDelay(kPoll);
      continue;
    }
    wants_frame_ = false;
    last_seq = frame.info().sequence;
    const int64_t send_started = esp_timer_get_time();
    const auto bytes = frame.bytes();
    char part[96];
    const int n = std::snprintf(part, sizeof part,
                                "--%s\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                                kBoundary, static_cast<unsigned>(bytes.size()));
    // The FrameRef keeps the slot alive while the bytes go out (ADR-0003).
    if (httpd_resp_send_chunk(req, part, n) != ESP_OK ||
        httpd_resp_send_chunk(req, reinterpret_cast<const char*>(bytes.data()),
                              static_cast<ssize_t>(bytes.size())) != ESP_OK ||
        httpd_resp_send_chunk(req, "\r\n", 2) != ESP_OK) {
      wants_frame_ = false;
      return;  // viewer went away
    }
    // Pace: idle at least as long as the frame took to send, so the stream never takes more than
    // half of the link and the UI, uploads and buttons stay responsive.
    const int64_t took = esp_timer_get_time() - send_started;
    const int64_t pause = std::max(took, kMinFramePeriodUs - took);
    vTaskDelay(pdMS_TO_TICKS(pause / 1000));
    wants_frame_ = true;
  }
  wants_frame_ = false;
  httpd_resp_send_chunk(req, nullptr, 0);
}

}  // namespace device
