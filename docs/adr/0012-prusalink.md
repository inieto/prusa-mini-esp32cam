# ADR-0012: Read the printer state from PrusaLink

- Status: accepted · 2026-11-09

## Context
A timelapse per print and "upload only while printing" both need to know what the printer is
doing. The MINI/MK4 firmware exposes PrusaLink on the LAN: `GET /api/v1/status` (printer state,
job id, progress, time remaining) and `GET /api/v1/job` (file name), protected by HTTP Digest
(user `maker`, password from the printer menu). PrusaConnect also knows the state, but its
camera API does not expose it.

## Decision
- A `PrinterMonitor` task polls `/api/v1/status` every 10 s (60 s after 3 failures) with
  `esp_http_client` and Digest auth over plain HTTP on the LAN, and fetches `/api/v1/job` only
  when the job id changes. The result is cached behind the core `PrinterInfo` port: the capture
  task and the web server never wait on the printer.
- Parsing and the state mapping (`PRINTING`, `PAUSED`, `FINISHED`/`STOPPED`, `IDLE`/`READY`,
  `BUSY`, `ERROR`/`ATTENTION`) are pure core code with tests built from the OpenAPI spec.
- `PrintActivity` (core) decides whether uploads to PrusaConnect are worth it: active while
  printing, paused, busy or in error, and for 5 minutes after a print ends. **It fails open**: if
  PrusaLink is not configured or unreachable the camera keeps uploading.
- A skipped upload is not a network failure: the recovery ladder (ADR-0004) is untouched
  (covered by a test running two idle hours).
- The PrusaLink password is a secret like the others: stored in NVS, never returned by the API.

## Consequences
+ Per-print timelapse folders named after the file; fewer useless uploads while idle.
− One more task (6 KB stack) and an HTTP request every 10 s on the LAN.
− Plain HTTP to the printer, as PrusaLink itself offers; Digest keeps the password off the wire.
