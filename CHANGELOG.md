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
- Portable core with 46 tests on the Mac (GoogleTest + AddressSanitizer/UBSan).
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
- OTA via web with rollback, downloadable coredump, logs and timelapse on SD, PrusaLink.
