# PrusaCam byClaude

Firmware nuevo para **ESP32-CAM AI Thinker (OV2640)** que publica snapshots en **PrusaConnect**.
Reescritura desde cero de [Prusa-Firmware-ESP32-Cam](https://github.com/prusa3d/Prusa-Firmware-ESP32-Cam)
v1.1.2, con arquitectura hexagonal, actores y seguridad por defecto.

- Arquitectura: [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)
- Decisiones: [`docs/adr/`](docs/adr/)
- Para agentes de IA y contribuidores: [`AGENTS.md`](AGENTS.md)
- Hallazgos sobre v1.1.2 que motivan el diseño: auditoría fuera del repo por ahora (IDs B*, S*, F*)

## Estado

| Corte | Estado |
|---|---|
| Núcleo portable (políticas, protocolo PrusaConnect, EXIF, FramePool, JSON, bundle UI) + 46 tests en host | ✅ |
| Adaptadores ESP-IDF: cámara, cliente PrusaConnect, WiFi/AP/mDNS/SNTP, NVS | ✅ compila, sin probar en hardware |
| Servidor HTTP: login (PBKDF2 + cookie), API REST `/api/v1`, UI embebida gzip (18 KB) | ✅ compila, sin probar en hardware |
| Diagnóstico: log en RAM visible en la web, historial de reinicios con uptime previo | ✅ compila, sin probar en hardware |
| UI web con estética Prusa (validada contra el servidor mock) | ✅ |
| OTA por web con rollback, coredump por API, logs en SD, timelapse, PrusaLink | ⏳ |

## Primer uso

1. Sin WiFi configurado, la cámara levanta el AP `PrusaCam-XXXX` en `http://192.168.4.1`. La
   clave es única por dispositivo y se imprime en el log serie al arrancar.
2. No hay contraseña de fábrica: al abrir la web se crea el usuario. Por seguridad, eso solo se
   puede hacer **durante los primeros 15 minutos** después de encender (si pasa el tiempo,
   reiniciá la cámara).
3. En la pestaña *PrusaConnect* se carga el token; el intervalo se elige en la web de PrusaConnect.

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
python3 tools/mock_server.py 8080   # http://localhost:8080 (primero pide crear usuario)
```

## Licencia

GPL-3.0 (deriva estilos e íconos del proyecto original). El logo de Prusa es marca registrada
de Prusa Research: uso personal; reemplazarlo si se distribuye.
