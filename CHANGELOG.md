# Changelog

Format based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow
[SemVer](https://semver.org/). References like B1, S4 or F2 point to the findings about v1.1.2
(audit not yet published in the repo).

## [Unreleased]

### Added
- Status LED: the red LED on the back blinks once a minute when all is well, blinks slowly
  without WiFi and double-blinks when PrusaConnect uploads fail. Can be switched off in *System*.
- Sharpness and noise reduction (OV3660/OV5640) and a fixed white balance preset (all sensors)
  in *Camera*.
- The last 6 KB of log survive crashes, watchdogs and software reboots (RTC memory, not power
  loss): *System → Before the last reboot* shows them with the reset reason (ADR-0017).
- Read-only API token (*System*): snapshot, live stream and status for integrations without
  logging in, via `Authorization: Bearer` or `?token=`; it cannot change anything (ADR-0016).

### Pending
- 48 h soak against PrusaConnect on hardware.
- Hardware validation of web OTA, SD timelapse, PrusaLink, MQTT, GPIO12 factory reset, QXGA and
  OV2640 framing.

## [1.0.0] - 2026-11-10

### Added
- Light intensity slider (1–100 %, *Camera → Light and flash*) for the Light button and the flash
  on capture, applied at once; the LED PWM went from 8 to 10 bits for fine low-end steps.
- *System* shows total internal RAM and PSRAM with the free percentage.
- Framing: digital zoom (up to the sensor's limit, e.g. 2.56× at 800×600 with the OV3660) and
  pan, done by the sensor's windowing so zoomed frames keep real detail and cost nothing extra.
  Adjusted live from *Camera → Framing* while watching the live view (ADR-0015).
- Separate JPEG quality for the live view (default 30, snapshots keep theirs): the sensor
  encodes in hardware, so lighter live frames cost nothing and leave WiFi room for the rest.
- Configurable WiFi transmit power (*Network*), applied at once. Lower peaks help marginal
  5 V supplies, where heavy traffic made the router drop the camera (reason 34, missing ACKs).

### Fixed
- An upload on a keep-alive connection the server had already closed counted as a failure and
  waited 11–45 s; it is now retried at once on a fresh connection. Seen on hardware.
- The live view saturated the camera's WiFi (frames sent back to back), stalling Refresh, Light
  and page loads: it now paces itself to at most half the link and 4 fps, captures frames on
  demand, and TCP buffers doubled. Seen on hardware.

## [0.2.0] - 2026-11-10

First tagged release. Runs on an AI Thinker board with OV3660 (web UI, login, WiFi, fallback AP,
PrusaConnect uploads, SD mount verified on hardware).

### Added
- On-device web server with the embedded UI (gzip, ~22 KB, served with an ETag).
- Login without a factory password: the first user is created from the web UI during the first
  15 minutes after power-on. PBKDF2-SHA256, `HttpOnly; SameSite=Strict` cookie, 30 s lockout
  after 5 failed attempts (S1, S2, S5).
- REST API `/api/v1`: status, snapshot, light, reboot, logs, camera, PrusaConnect and network
  settings, WiFi scan. It never returns the token or passwords (S6).
- Diagnostics without a serial cable: live log in the web UI and history of the last 8 reboots
  with how long the camera had been running.
- Configurable gain ceiling, night exposure (AEC2) and flash intensity/lead time (F2: dark image).
- The snapshot interval is taken from the one chosen in the PrusaConnect web UI (`trigger_scheme`).
- Setup AP `PrusaCam-XXXX` at 192.168.4.1 with a per-device password (B12, S2).
- Fallback setup AP: if the configured WiFi cannot be reached for 3 minutes, the setup AP comes
  up alongside the station so the camera can be fixed without a serial cable. The station keeps
  retrying every 30 s and the AP closes once WiFi is back and nobody is using it. The
  connectivity-lost reboot is postponed while someone is connected to the AP (ADR-0009).
- WiFi diagnostics in the log: RSSI on every connection (warning below −75 dBm) and the
  disconnect reason by name on the first failure and every 10th.
- Creating the user asks to repeat the password. The login password fields have an eye button
  to show or hide what was typed.
- OV3660/OV5640 support: the sensor is detected at boot, reported in the Camera tab and written
  to the EXIF model (it was hard-coded "OV2640"). OV3660 adds 2048×1536 (QXGA); the UI only
  offers resolutions the sensor can deliver and the API rejects the rest.
- Factory reset: hold GPIO12 to GND for 10 s at power-on (same gesture as v1.1.2; the red LED
  blinks, then stays on until the jumper is removed), or *System → Factory reset* in the web UI.
  It forgets the web user, WiFi, token and camera settings; the setup AP password stays.
- Saving the WiFi settings while the camera has no working station (setup or fallback AP)
  reboots it straight away to join the new network.
- Support for BOYA flash chips found on some AI Thinker clones.
- Firmware update from the web UI (*System → Firmware*), with upload progress. The image is
  checked (same project, SHA-256) and installed in the other slot; it must run 60 s with the
  network up or the previous version comes back on the next reset. A rollback is reported in
  the UI (ADR-0010).
- Crash dumps: after a panic the *System* tab shows the task, PC and reason, and the ELF can be
  downloaded for `idf.py coredump-info` or erased.
  With an SD card the dump is moved to `/coredumps/` at boot, keeping a history.
- SD card support (1-bit mode, FAT32): the log is kept in one file per day with 14-day rotation
  (F6), and a timelapse can record always or only while printing (one folder per print). New
  *Storage* tab with the settings and a file browser to open, download and delete files
  (ADR-0011).
- PrusaLink: with the printer address and its PrusaLink password (*Printer* tab), the camera
  shows the printer state, progress and file, records the timelapse per print and can upload to
  PrusaConnect only while printing (and 5 min after). An unreachable printer never stops the
  uploads (ADR-0012).
- MQTT / Home Assistant (*MQTT* tab): the camera shows up through MQTT discovery with a camera
  entity fed by every snapshot, a light switch, a snapshot button and sensors for PrusaConnect,
  the printer, WiFi signal and uploads (ADR-0013).
- Live view: an MJPEG stream (up to 5 fps, one viewer) from the *Live view* button, served
  without blocking the rest of the web UI. Live frames never fire the flash or get uploaded
  (ADR-0014).
- `tools/soak.py`: long-running reliability check against a real camera (upload success
  rate, reboots, RSSI, minimum heap) with a PASS/FAIL verdict.
- Portable core with 64 tests on the Mac (GoogleTest + AddressSanitizer/UBSan).
- `AGENTS.md` (and `CLAUDE.md`, which imports it) with architecture invariants, commands and
  conventions for AI agents and contributors; ADR index and template.

### Changed
- Complete rewrite on ESP-IDF 5.5.5 and C++23, without Arduino (ADR-0001).
- One owner per resource and reference-counted frames: the races between the web server, the
  stream and the uploads are gone (B4, B7–B10).
- TLS with the ESP-IDF CA bundle and buffers in PSRAM, over a persistent connection (S7, B6).
- A PrusaConnect error no longer drops WiFi: retries with backoff and staged recovery only when
  the network is really failing (B1, B2).
- The brownout detector stays enabled and low-voltage reboots are recorded.
- Code, docs, ADRs, web UI and changelog are now in English.

### Fixed
- *Refresh* now takes a fresh local frame (not uploaded) and shows it within a second; before
  it only re-read the last scheduled capture, up to one interval old.
- OV3660 frames overflowed the camera driver's default JPEG buffer (`FB-OVF`, frames dropped):
  the buffer is now sized explicitly per resolution. Seen on hardware.
- The web UI was shown as raw HTML: the `Content-Type` of the UI files pointed to memory that
  had already been freed. Tested on hardware (board with OV3660).
- The UI ETag now includes the firmware hash, so an update that changes headers is not hidden
  behind a stale response in the browser cache.

### Compatibility
- The v1.1.2 *fingerprint* is reproduced: with the same token, PrusaConnect keeps recognising the
  migrated camera.

[Unreleased]: https://github.com/inieto/prusa-mini-esp32cam/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/inieto/prusa-mini-esp32cam/compare/v0.2.0...v1.0.0
[0.2.0]: https://github.com/inieto/prusa-mini-esp32cam/releases/tag/v0.2.0
