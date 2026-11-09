# Architecture — PrusaCam byClaude

New firmware for the **AI Thinker ESP32-CAM (ESP32-D0WDQ6 + OV2640 + 4 MB PSRAM + 4 MB flash)**
whose main goal is to publish snapshots to **PrusaConnect** reliably, securely and observably.
It replaces Prusa-Firmware-ESP32-Cam v1.1.2; the S*, B*, F* IDs refer to findings from an audit of
that firmware (not yet published in the repo).

## 1. Goals and non-goals

**Goals (in priority order)**
1. Upload snapshots to PrusaConnect without gaps or avoidable reboots (≥ 99 % successful uploads over 48 h).
2. Secure by default on a home LAN (no anonymous endpoints that change state).
3. Diagnosable without a serial cable: metrics, reboot history, core dumps.
4. Extensible: adding a destination (MQTT, RTSP, timelapse) does not touch the core.
5. The logic is tested on the Mac, without hardware.

**Non-goals**
- On-device vision-based failure detection (no RAM/CPU for it; left as a future remote port).
- Supporting other boards in v1 (the design allows it via `boards/`, but it is not tested).
- Configuration compatibility with v1.1.2 (reconfigure once).

## 2. Platform constraints

| Resource | Available | Design implication |
|---|---|---|
| Internal RAM | ~320 KB, ~150 KB free with WiFi | TLS, WiFi and stacks live here → **no heap on the hot path**; mbedTLS buffers in PSRAM |
| PSRAM | 4 MB (slow, no DMA for WiFi) | JPEG frames and the buffer pool |
| CPU | 2× Xtensa 240 MHz | core 0: WiFi/LwIP; core 1: application |
| Flash | 4 MB | 2 OTA slots of 1.875 MB + NVS + coredump |
| Power | marginal supply (long wires) | avoid spikes: no periodic WiFi scans, configurable TX power |

## 3. Layered view (hexagonal)

```
             ┌──────────────────────────────────────────────┐
             │                core (pure C++23)             │
             │  domain/   values, policies, rules           │
             │  app/      use cases (orchestrate ports)     │
             │  ports/    interfaces the core NEEDS         │
             │  protocol/ PrusaConnect, EXIF (pure bytes)   │
             └───────────────▲──────────────────▲───────────┘
                             │ implement        │ implement
   ┌─────────────────────────┴──┐        ┌──────┴──────────────────────┐
   │ ESP-IDF adapters (device)  │        │ fakes (test/host)            │
   │ camera, http_client       │        │ FakeCamera, FakeClock,       │
   │ wifi, nvs_store, sd, httpd │        │ FakeSnapshotSink, …          │
   └────────────────────────────┘        └──────────────────────────────┘
                 ▲
          main/ (composition root: creates actors, wires ports, starts)
```

Dependency rule: `core` **includes nothing from ESP-IDF or FreeRTOS**. It compiles with Apple
clang on the Mac and with Xtensa GCC on the device. Only `adapters/` and `main/` know the platform.

## 4. Concurrency model: actors

The root cause of v1.1.2's worst bugs (B4, B7–B10) was several tasks sharing the frame buffer and
the sensor. Here every resource has **a single owner** (a task with its queue) and everyone else
sends it messages.

| Actor (task) | Core | Owns | Receives |
|---|---|---|---|
| `CameraActor` | 1 | `esp32-camera` driver, sensor, flash LED | `Capture`, `ApplySettings` |
| `SnapshotService` | 1 | capture schedule, `FramePool` | ticks, `TriggerNow`, network events |
| `ConnectUplink` | 1 | HTTPS client to PrusaConnect (keep-alive) | `Publish(frame)`, `SyncInfo` |
| `NetworkActor` | 0 | WiFi STA/AP, NTP, mDNS | WiFi events, recovery orders |
| `StorageActor` | 1 | SD card, log to disk | `AppendLog`, `WriteFile`, `Erase` |
| `httpd` (ESP-IDF's) | 1 | HTTP sockets | requests; **never blocks** longer than a `send` to a queue |

### 4.1 Frames: `FramePool` + `FrameRef`
- At boot N slots are reserved in PSRAM (N=3, size = worst-case JPEG at the configured resolution).
- `CameraActor` captures, **copies** the JPEG into a slot (prepending EXIF) and returns the `fb`
  to the driver right away → the driver never has buffers on loan.
- Consumers receive a `FrameRef`: an immutable pointer with an **atomic reference count**; the
  slot goes back to the pool when the last reference is destroyed (RAII). Upload, web and SD can
  read the same frame in parallel without locks and without use-after-free.
- If no slot is free, the capture is skipped (`frames_dropped` metric) instead of blocking.

## 5. Main flow: snapshot → PrusaConnect

```
tick (1 s) ──► SnapshotScheduler.due(now)? ──no──► done
                     │ yes
                     ▼
           CameraActor.Capture ──► FrameRef ──► ConnectUplink.Publish
                                          │            │
                                          │            ├─ 204/200 → policy.onSuccess
                                          │            ├─ 401/403 → "token rejected" state (no retry)
                                          │            └─ network/TLS/5xx → exponential backoff with jitter
                                          └──► (optional) timelapse to SD
```

- The interval comes from the `trigger_scheme` the user picks **in the PrusaConnect web UI**
  (response to `PUT /c/info`); the local value is only the default.
- `ConnectivityRecovery` decides when to escalate: *retry → reconnect WiFi → reboot*, with
  thresholds in minutes without **any** network success. A backend failure never drops WiFi by
  itself (B1). See ADR-0004.
- If the configured WiFi is unreachable for 3 minutes, the setup AP comes up alongside the station
  so the camera can be reached and fixed without a serial cable (ADR-0009).

## 6. Configuration

- A single typed schema (`core/domain/config.hpp`): each field with range, default and whether it
  needs a reboot. The REST API and the UI validate against the same schema.
- Persisted in **NVS** (no EEPROM emulation) with `schema_version` and explicit migrations.
- Secrets (WiFi password, token, web password) never leave through the API: they show as `"set": true`.

## 7. Security (threat model: home LAN + neighbours within WiFi range)

- **First boot**: service AP with a **per-device** password (derived from the MAC + salt, printed
  on the serial log) at `192.168.4.1`; it closes once WiFi is configured. It also comes back as a
  fallback when the configured WiFi is unreachable (ADR-0009).
- **Web**: username/password login → `HttpOnly; SameSite=Strict` session cookie. Every mutation
  is `POST`/`PUT` with `Content-Type: application/json` (CSRF blocked by SameSite + preflight).
  Automated clients use `Authorization: Bearer <api-token>`.
- **No anonymous endpoints** except `/login` and the static assets of the login screen.
- **OTA**: authenticated `POST /api/v1/ota`; image checked (same project, SHA-256) and
  **automatic rollback** unless the new version runs 60 s with the network up (ADR-0010; not
  signed for now).
- **TLS**: ESP-IDF CA bundle (covers the Let's Encrypt transition, S7).
- UI never uses `innerHTML` with data: everything via `textContent` (XSS-safe by construction, S4).

## 8. Observability

- `GET /api/v1/status`: uptime, internal/PSRAM heap (current and minimum), RSSI, successful/failed
  uploads, last error, reason for the last 8 reboots (in NVS), version.
- Log in a **RAM ring buffer** (visible instantly in the web UI) + batched dump to SD (every 10 s
  or 4 KB), with rotation that **deletes** old files (F6).
- WiFi diagnostics in the log: RSSI on every connection (warning below −75 dBm) and named
  disconnect reasons.
- `coredump` partition: after a panic, the next boot reports it (task, PC, reason) and the ELF
  can be downloaded from *System* (`GET /api/v1/coredump`).

## 9. Web UI

- Prusa look (colour/typography tokens from the original UI, see `web/src/styles.css`).
- Vanilla JS with ES modules, no jQuery or frameworks (no Node build); served gzipped from flash
  with a content-hash `ETag` (UI bundle hash + firmware build hash).
- Screens: Snapshot/status · Camera · PrusaConnect · Network · System (logs, OTA, reboots).

## 10. Repository layout

```
firmware-byclaude/
  docs/ARCHITECTURE.md, docs/adr/        decisions
  firmware/                              ESP-IDF project
    main/                                composition root
    components/core/                     portable core (domain, app, ports, protocol)
    components/device/                   ESP-IDF implementations + HTTP server
    boards/ai_thinker.hpp                pins and capabilities
    partitions.csv, sdkconfig.defaults
  web/src/                               UI (HTML/CSS/JS)
  test/host/                             core tests on the Mac (GoogleTest)
  tools/                                 scripts (web packing, mock server, etc.)
```

## 11. Test strategy

1. **Host (Mac)**: the whole `core` with GoogleTest + fakes; runs in seconds, on every commit.
2. **Integration**: a fake PrusaConnect in Python on the Mac; the real camera points at it
   (configurable hostname) to reproduce 401/503/timeouts.
3. **On hardware**: 48 h soak against the real PrusaConnect, watching `/api/v1/status`.

## 12. Vertical slices

1. **Snapshot → PrusaConnect** with Kconfig/NVS config, metrics and logs. *(minimum useful)*
2. Web: login, status, snapshot, camera/Connect/network settings.
3. OTA with rollback + coredump.
4. SD: persistent logs, timelapse, file browser.
5. PrusaLink (upload only while printing), MQTT/Home Assistant, stream.
