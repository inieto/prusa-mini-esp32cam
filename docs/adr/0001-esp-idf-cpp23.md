# ADR-0001: ESP-IDF v5.5.5 + C++23, sin Arduino

- Estado: aceptada · 2026-09-27

## Contexto
v1.1.2 usa arduino-esp32 sobre ESP-IDF, lo que oculta configuración clave (mbedTLS en PSRAM,
bundle de CAs, rollback de OTA, coredump) y mezcla APIs bloqueantes. Se evaluaron Rust
(`esp-idf-svc`) y Go (TinyGo, sin WiFi/cámara/TLS en ESP32 → descartado).

## Decisión
ESP-IDF **v5.5.5** (última 5.x; `esp32-camera` 2.1.x exige ≥ 5.1) en **C++23**
(`std::expected`, `std::span`), sin excepciones ni RTTI. El núcleo es C++ portable.

## Consecuencias
+ Acceso directo a sdkconfig, drivers nativos, binarios chicos, mismo lenguaje que ESP-DL.
− Se pierde el flujo "abrir en Arduino IDE"; el build es `idf.py`.
− Pasar a IDF 6.x será un ADR aparte cuando `esp32-camera` lo declare probado.
