# ADR-0001: ESP-IDF v5.5.5 + C++23, no Arduino

- Status: accepted · 2026-09-27

## Context
v1.1.2 uses arduino-esp32 on top of ESP-IDF, which hides key settings (mbedTLS in PSRAM, CA
bundle, OTA rollback, coredump) and mixes blocking APIs. Rust (`esp-idf-svc`) and Go were
evaluated (TinyGo has no WiFi/camera/TLS on the ESP32 → rejected).

## Decision
ESP-IDF **v5.5.5** (latest 5.x; `esp32-camera` 2.1.x requires ≥ 5.1) in **C++23**
(`std::expected`, `std::span`), without exceptions or RTTI. The core is portable C++.

## Consequences
+ Direct access to sdkconfig, native drivers, small binaries, same language as ESP-DL.
− The "open in Arduino IDE" workflow is gone; the build is `idf.py`.
− Moving to IDF 6.x will be a separate ADR once `esp32-camera` declares it tested.
