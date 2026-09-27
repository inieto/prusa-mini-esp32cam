# AGENTS.md

Guide for AI coding agents (and humans) working on **PrusaCam byClaude**: firmware for the
**AI Thinker ESP32-CAM (ESP32-D0WDQ6, OV2640, 4 MB PSRAM, 4 MB flash)** whose job is to publish
snapshots to **PrusaConnect** reliably, securely and observably.

Language: this file is in English for reach. Architecture docs, ADRs, the web UI, the changelog and
commit messages are in **Spanish** — keep them that way. Code, identifiers and code comments are in
English.

## Read first

1. `docs/ARCHITECTURE.md` — goals, constraints, layers, actors, main flow.
2. `docs/adr/` — every architectural decision and *why*. Do not contradict an accepted ADR
   silently: write a new ADR that supersedes it (see "Decisions" below).
3. `CHANGELOG.md` — what exists today and what is pending.
4. IDs like **B1, S4, F2** in code/docs refer to an audit of the original Prusa-Firmware-ESP32-Cam
   v1.1.2 that is not published in this repo yet (it describes unpatched upstream issues). The
   one-line meaning is usually next to the ID; B = stability, S = security, F = functional bug.

## Repository map

```
docs/ARCHITECTURE.md, docs/adr/     design and decisions
firmware/                           ESP-IDF project (main/, components/, partitions.csv, sdkconfig.defaults)
  components/core/                  portable C++23 core: NO ESP-IDF/FreeRTOS includes, host-testable
    include/core/ports.hpp          interfaces the core needs (camera, PrusaConnect, network, system, log)
    include/core/snapshot_service   main use case: capture → upload, retries, recovery ladder
  components/device/                ESP-IDF adapters implementing the ports + HTTP server
  main/main.cpp                     composition root (wires everything, starts tasks)
test/host/                          GoogleTest suite for core (runs on macOS/Linux with sanitizers)
web/src/                            web UI (vanilla HTML/CSS/JS, Prusa look), packed into flash
tools/pack_web.py                   packs web/src into the gzip bundle embedded in the firmware
tools/mock_server.py                fake /api/v1 to develop the UI without hardware (API contract)
```

## Commands

Host tests (fast, run them on every change to `components/core` or `web/src`):

```sh
cmake -S test/host -B build/host -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host && ctest --test-dir build/host --output-on-failure
```

Firmware (ESP-IDF **v5.5.5**, expected at `~/esp/esp-idf-v5.5.5`):

```sh
cd firmware
. ~/esp/esp-idf-v5.5.5/export.sh
idf.py build            # must finish with zero warnings: components use -Werror
idf.py size             # keep the app well under the 1.875 MB OTA slot
```

UI without hardware:

```sh
python3 tools/mock_server.py 8080   # http://localhost:8080, first load asks to create a user
```

Flashing (`idf.py -p <port> flash monitor`) needs a human: IO0 must be jumpered to GND while
powering on. **Never flash on your own** — ask the maintainer, and make a backup first
(`esptool.py read_flash 0 0x400000 backup.bin`).

## Architecture invariants (do not break)

1. **`components/core` never includes ESP-IDF or FreeRTOS headers.** It must keep compiling with
   the host toolchain. Platform code goes in `components/device` behind a port (ADR-0002).
2. **One owner per resource.** Only the task running `SnapshotService` touches the camera driver
   and sensor. Other tasks *request* (e.g. `Ov2640Camera::request_settings`,
   `SnapshotService::request_snapshot`). HTTP handlers never call drivers (ADR-0003).
3. **Frames are shared through `FrameRef`** (reference-counted slots of a preallocated
   `FramePool`). Never keep raw pointers to frame bytes beyond the life of a `FrameRef`; never
   hold `camera_fb_t` outside `Ov2640Camera::capture`.
4. **No heap allocation in the capture/upload hot path**; buffers are reserved at boot. Internal
   RAM is scarce (TLS + WiFi); large buffers go to PSRAM.
5. **Backend errors never tear down WiFi.** Recovery escalates (reconnect → reboot) only while the
   network is actually failing (ADR-0004). There is a test guarding this
   (`BackendErrorsBackOffButNeverTouchWifi`); do not weaken it.
6. **Errors are values**: `core::Result<T>` (`std::expected`). No exceptions, no RTTI.
7. **Security defaults (ADR-0005)**: no factory password; mutations are `POST`/`PUT` with a JSON body
   (CSRF defence together with `SameSite=Strict`); the API never returns secrets (report
   `*_set: true` instead); secrets and session tokens come from `esp_fill_random()` (hardware RNG),
   never from the core's xorshift (that one is only for backoff jitter).
8. **Web UI renders device data with `textContent` only** — never `innerHTML` with data (S4:
   neighbours control SSID strings).
9. **PrusaConnect compatibility**: `core::connect::legacy_fingerprint()` must keep producing the
   exact v1.1.2 value (test vector in `prusa_connect_test.cpp`); changing it orphans every migrated
   camera. Never send `trigger_scheme` in `/c/info` — the user picks it in PrusaConnect.
10. **Do not enable Secure Boot or Flash Encryption, and do not disable the brownout detector.**
    The first two burn eFuses irreversibly; the last one hides real power problems.

## Conventions

- C++23, GCC 14 (Xtensa) and Apple clang both must accept the code. `-Wall -Wextra -Werror`.
- Match the surrounding style: `snake_case` functions/variables, `PascalCase` types, `kConstant`,
  trailing `_` for members, 2-space indent, ~100 columns. Comment the *why*, not the *what*.
- New behaviour in `core` comes with a host test. Bugs get a regression test first.
- The API contract lives in two places that must stay in sync: `device/src/web_server.cpp` and
  `tools/mock_server.py`. Change both, then check the UI against the mock.
- Settings stored as NVS blobs are versioned (`SettingsStore::kCameraSchema`): bump the version
  when `CameraSettings` changes layout.
- UI strings in Spanish (rioplatense). Visual language: tokens at the top of `web/src/styles.css`
  (orange `#FA6831`, greys, white buttons that turn orange on hover). No frameworks, no build step.
- Keep the web bundle small (currently ~18 KB gzip, max 32 assets).

## Decisions (ADRs)

Any change to architecture, dependencies, memory layout, security model or external protocols needs
an ADR: copy `docs/adr/0000-template.md` to the next number, set it to *propuesta*, and link it from
the PR. Superseding an ADR means a new ADR plus marking the old one *reemplazada por ADR-XXXX*.

## Commits, changelog, PRs

- Commit messages in Spanish: short imperative subject, body explaining what and why, finding IDs
  where relevant (e.g. "corrige B8"). One logical change per commit.
- Update `CHANGELOG.md` → `[Sin publicar]` for anything user-visible.
- Before committing: host tests green, `idf.py build` clean, UI checked against the mock if touched.
- State clearly in the PR/commit whether it was **tested on hardware** or only built/host-tested.

## Current status and roadmap

See `CHANGELOG.md` (`[Sin publicar]` → *Pendiente*). At the time of writing the firmware builds
and the core is tested, but it has **not yet run on hardware**. Next steps, in order:
1. First flash and bring-up on the real board; 48 h soak against PrusaConnect.
2. OTA upload from the web UI with rollback (`BOOTLOADER_APP_ROLLBACK_ENABLE` is already on) and
   coredump download.
3. SD card: persistent logs (batched), timelapse per print, file browser.
4. PrusaLink (Mini+ local API): upload only while printing.
5. MQTT / Home Assistant, MJPEG/RTSP stream.

Out of scope for this hardware: on-device vision/failure detection (no RAM/CPU). If added, it goes
behind a `FrameAnalyzer` port with a *remote* adapter.

## Hardware facts agents get wrong

- PSRAM is required; frames and TLS buffers live there (`sdkconfig.defaults`).
- GPIO4 = white flash LED, also SD DATA1 → the SD card must run in **1-bit** mode.
- GPIO33 = red status LED, active low. GPIO12 is a strapping pin (flash voltage): do not pull it
  high at boot.
- LEDC timer/channel 0 drive the camera XCLK; the flash LED uses timer/channel 1.
- The board's 5 V input is often marginal (long wires): brownout resets are *data*, record them.
