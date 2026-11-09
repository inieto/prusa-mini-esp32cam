# ADR-0014: MJPEG live view through an async HTTP handler, one viewer

- Status: accepted · 2026-11-09

## Context
Checking the first layer or a spaghetti print needs more than a snapshot every 30 s. v1.1.2
streamed from the same task that served the UI and shared the driver's frame buffer between
tasks — the races behind B4/B7–B10. ESP-IDF's HTTP server runs every handler on one task, so a
naive endless stream handler freezes the web UI.

## Decision
- `GET /api/v1/stream` (authenticated, same cookie, same origin: no CSP or CORS exceptions)
  answers `multipart/x-mixed-replace`. The handler calls `httpd_req_async_handler_begin` and
  hands the request to a dedicated stream task; the HTTP task is free again immediately.
- **One viewer at a time** (503 for a second one). The stream sends the latest `FrameRef`
  whenever its sequence changes; the reference keeps the slot alive while it is sent.
- Frames still come only from the capture task (ADR-0003): while someone watches, it adds
  **preview captures** every 200 ms (≤ 5 fps) between its 1 s ticks. Previews never fire the
  flash, never upload and never reach observers (timelapse, MQTT); they only become the latest
  frame. A busy pool just skips a preview.
- The FramePool grows to 4 slots when the sensor tops out at UXGA (3 at QXGA, where PSRAM is
  full): latest frame + upload + stream + a timelapse/MQTT hand-over.
- A stream stops after 30 minutes, when the viewer closes the connection, or when the page is
  hidden.

## Consequences
+ Live view without blocking the UI, uploads or the timelapse.
− Frame rate depends on resolution and WiFi (a QXGA frame alone is ~300–600 KB).
− When all HTTP sockets are busy, LRU purging may close the stream first (the UI just stops it).
