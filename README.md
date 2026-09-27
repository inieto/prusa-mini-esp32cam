# PrusaCam byClaude

Firmware nuevo para **ESP32-CAM AI Thinker (OV2640)** que publica snapshots en **PrusaConnect**.
Reescritura desde cero de [Prusa-Firmware-ESP32-Cam](https://github.com/prusa3d/Prusa-Firmware-ESP32-Cam)
v1.1.2, con arquitectura hexagonal, actores y seguridad por defecto.

- Arquitectura: [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)
- Decisiones: [`docs/adr/`](docs/adr/)
- Hallazgos sobre v1.1.2 que motivan el diseño: [`../PLAN-firmware.md`](../PLAN-firmware.md)

## Estado

| Corte | Estado |
|---|---|
| Núcleo portable (políticas, protocolo PrusaConnect, EXIF, FramePool) + 41 tests en host | ✅ |
| Adaptadores ESP-IDF: cámara, cliente PrusaConnect, WiFi/AP/mDNS/SNTP, NVS | ✅ compila, sin probar en hardware |
| UI web con estética Prusa (contra servidor mock) | ✅ |
| Servidor HTTP en el dispositivo (login, API REST, UI embebida) | ⏳ siguiente |
| OTA con rollback, coredump por API, logs en SD, timelapse, PrusaLink | ⏳ |

## Requisitos

- ESP-IDF **v5.5.5** en `~/esp/esp-idf-v5.5.5` (`install.sh esp32`)
- `cmake` y `ninja` (Homebrew) para los tests en la Mac

## Tests del núcleo (Mac)

```sh
cmake -S test/host -B build/host -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host && ctest --test-dir build/host --output-on-failure
```

Corren con AddressSanitizer y UndefinedBehaviorSanitizer.

## Firmware

```sh
cd firmware
. ~/esp/esp-idf-v5.5.5/export.sh
idf.py menuconfig          # opcional: "PrusaCam byClaude (bring-up)" → WiFi y token de desarrollo
idf.py build
idf.py -p /dev/cu.usbserial-XXXX flash monitor   # IO0 a GND al encender para entrar en modo flash
```

Migración desde v1.1.2: el firmware reproduce el *fingerprint* que usaba v1.1.2, así que con el
mismo token PrusaConnect sigue reconociendo la cámara.

## UI sin hardware

```sh
python3 tools/mock_server.py 8080   # http://localhost:8080  (admin / admin)
```

## Licencia

GPL-3.0 (deriva estilos e íconos del proyecto original). El logo de Prusa es marca registrada
de Prusa Research: uso personal; reemplazarlo si se distribuye.
