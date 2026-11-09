# PrusaCam byClaude

New firmware for the **AI Thinker ESP32-CAM (OV2640)** that publishes snapshots to
**PrusaConnect**. A from-scratch rewrite of
[Prusa-Firmware-ESP32-Cam](https://github.com/prusa3d/Prusa-Firmware-ESP32-Cam) v1.1.2, with a
hexagonal architecture, actors and secure defaults.

- Architecture: [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)
- Decisions: [`docs/adr/`](docs/adr/)
- For AI agents and contributors: [`AGENTS.md`](AGENTS.md)
- Findings about v1.1.2 that drive the design: audit kept outside the repo for now (IDs B*, S*, F*)

## Status

| Slice | Status |
|---|---|
| Portable core (policies, PrusaConnect protocol, EXIF, FramePool, JSON, UI bundle) + 46 host tests | ✅ |
| ESP-IDF adapters: camera, PrusaConnect client, WiFi/AP/mDNS/SNTP, NVS | ✅ running on hardware (board with OV3660) |
| HTTP server: login (PBKDF2 + cookie), REST API `/api/v1`, embedded gzipped UI (~18 KB) | ✅ running on hardware |
| Diagnostics: RAM log in the web UI, reboot history with previous uptime, WiFi RSSI/reasons | ✅ |
| Fallback setup AP when the WiFi is unreachable (ADR-0009) | ✅ builds, not yet tested on hardware |
| Web UI with the Prusa look (checked against the mock server) | ✅ |
| 48 h soak, OTA via web with rollback, coredump via API, SD logs, timelapse, PrusaLink | ⏳ |

## First use

1. Without WiFi configured, the camera raises the `PrusaCam-XXXX` AP at `http://192.168.4.1`.
   The password is unique per device and printed on the serial log at boot. The same AP comes
   back if the configured WiFi cannot be reached for 3 minutes.
2. There is no factory password: opening the web UI creates the user. For security this can only
   be done **during the first 15 minutes** after power-on (if the window closes, reboot the camera).
3. In the *PrusaConnect* tab, paste the token; the interval is chosen in the PrusaConnect web UI.

## Requirements

- ESP-IDF **v5.5.5** in `~/esp/esp-idf-v5.5.5` (`install.sh esp32`)
- `cmake` and `ninja` (Homebrew) for the tests on the Mac

## Core tests (Mac)

```sh
cmake -S test/host -B build/host -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host && ctest --test-dir build/host --output-on-failure
```

They run with AddressSanitizer and UndefinedBehaviorSanitizer.

## Firmware

```sh
cd firmware
. ~/esp/esp-idf-v5.5.5/export.sh
idf.py menuconfig          # optional: "PrusaCam byClaude (bring-up)" → development WiFi and token
idf.py build
idf.py -p /dev/cu.usbserial-XXXX flash monitor   # IO0 to GND at power-on to enter flash mode
```

`idf.py -p <port> app-flash` rewrites only the application and keeps NVS (user, WiFi, token).

Migrating from v1.1.2: the firmware reproduces the *fingerprint* v1.1.2 used, so with the same
token PrusaConnect keeps recognising the camera.

## UI without hardware

```sh
python3 tools/mock_server.py 8080   # http://localhost:8080 (asks to create a user first)
```

## License

GPL-3.0 (styles and icons derive from the original project). The Prusa logo is a registered
trademark of Prusa Research: personal use; replace it if you distribute this.
