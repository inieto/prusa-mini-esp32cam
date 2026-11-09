# Changelog

Format based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow
[SemVer](https://semver.org/). References like B1, S4 or F2 point to the findings about v1.1.2
(audit not yet published in the repo).

## [Unreleased]

### Added
- On-device web server with the embedded UI (gzip, ~18 KB, served with an ETag).
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
- `tools/soak.py`: long-running reliability check against a real camera (upload success
  rate, reboots, RSSI, minimum heap) with a PASS/FAIL verdict.
- Portable core with 47 tests on the Mac (GoogleTest + AddressSanitizer/UBSan).
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
- The web UI was shown as raw HTML: the `Content-Type` of the UI files pointed to memory that
  had already been freed. Tested on hardware (board with OV3660).
- The UI ETag now includes the firmware hash, so an update that changes headers is not hidden
  behind a stale response in the browser cache.

### Compatibility
- The v1.1.2 *fingerprint* is reproduced: with the same token, PrusaConnect keeps recognising the
  migrated camera.

### Pending
- 48 h soak against PrusaConnect on hardware.
- MJPEG stream.
