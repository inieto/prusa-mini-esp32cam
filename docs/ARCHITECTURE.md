# Arquitectura — PrusaCam byClaude

Firmware nuevo para **ESP32-CAM AI Thinker (ESP32-D0WDQ6 + OV2640 + 4 MB PSRAM + 4 MB flash)**
cuyo objetivo principal es publicar snapshots en **PrusaConnect** de forma confiable, segura y
observable. Reemplaza a Prusa-Firmware-ESP32-Cam v1.1.2; ver `../PLAN-firmware.md` para los
hallazgos que motivan cada decisión (IDs S*, B*, F*).

## 1. Objetivos y no-objetivos

**Objetivos (en orden de prioridad)**
1. Subir snapshots a PrusaConnect sin cortes ni reinicios evitables (≥ 99 % de subidas OK en 48 h).
2. Seguro por defecto en una LAN doméstica (sin endpoints anónimos que cambien estado).
3. Diagnosticable sin cable serie: métricas, historial de reinicios, core dumps.
4. Extensible: agregar un destino (MQTT, RTSP, timelapse) no toca el núcleo.
5. La lógica se prueba en la Mac, sin hardware.

**No-objetivos**
- Detección de fallas por visión en el dispositivo (no hay RAM/CPU; queda como puerto futuro remoto).
- Soportar otras placas en v1 (el diseño lo permite vía `boards/`, pero no se prueba).
- Compatibilidad de configuración con v1.1.2 (se reconfigura una vez).

## 2. Restricciones de la plataforma

| Recurso | Disponible | Implicancia de diseño |
|---|---|---|
| RAM interna | ~320 KB, ~150 KB libres con WiFi | TLS, WiFi y stacks van acá → **cero heap en caliente**; buffers de mbedTLS en PSRAM |
| PSRAM | 4 MB (lenta, sin DMA para WiFi) | frames JPEG y pool de buffers |
| CPU | 2× Xtensa 240 MHz | core 0: WiFi/LwIP; core 1: aplicación |
| Flash | 4 MB | 2 slots OTA de 1,875 MB + NVS + coredump |
| Energía | alimentación marginal (cables largos) | evitar picos: sin escaneo WiFi periódico, TX power configurable |

## 3. Vista de capas (hexagonal)

```
             ┌──────────────────────────────────────────────┐
             │                 core (C++23 puro)            │
             │  domain/   valores, políticas, reglas         │
             │  app/      casos de uso (orquestan puertos)   │
             │  ports/    interfaces que el núcleo NECESITA  │
             │  protocol/ PrusaConnect, EXIF (bytes puros)   │
             └───────────────▲──────────────────▲───────────┘
                             │ implementan      │ implementan
   ┌─────────────────────────┴──┐        ┌──────┴──────────────────────┐
   │ adapters ESP-IDF (device)  │        │ fakes (test/host)            │
   │ camera_ov2640, http_client │        │ FakeCamera, FakeClock,       │
   │ wifi, nvs_store, sd, httpd │        │ FakeSnapshotSink, …          │
   └────────────────────────────┘        └──────────────────────────────┘
                 ▲
          main/ (composition root: crea actores, conecta puertos, arranca)
```

Regla de dependencias: `core` **no incluye nada de ESP-IDF ni FreeRTOS**. Compila con
Apple clang en la Mac y con GCC Xtensa en el dispositivo. Solo `adapters/` y `main/` conocen la
plataforma.

## 4. Modelo de concurrencia: actores

La causa raíz de los bugs más graves de v1.1.2 (B4, B7–B10) fue que varias tareas compartían el
frame buffer y el sensor. Acá cada recurso tiene **un único dueño** (una tarea con su cola) y el
resto le manda mensajes.

| Actor (tarea) | Core | Dueño de | Recibe |
|---|---|---|---|
| `CameraActor` | 1 | driver `esp32-camera`, sensor, LED flash | `Capture`, `ApplySettings` |
| `SnapshotService` | 1 | agenda de capturas, `FramePool` | ticks, `TriggerNow`, eventos de red |
| `ConnectUplink` | 1 | cliente HTTPS a PrusaConnect (keep-alive) | `Publish(frame)`, `SyncInfo` |
| `NetworkActor` | 0 | WiFi STA/AP, NTP, mDNS | eventos WiFi, órdenes de recuperación |
| `StorageActor` | 1 | tarjeta SD, log a disco | `AppendLog`, `WriteFile`, `Erase` |
| `httpd` (de ESP-IDF) | 1 | sockets HTTP | requests; **nunca bloquea** más que un `send` a una cola |

### 4.1 Frames: `FramePool` + `FrameRef`
- Al arrancar se reservan N slots en PSRAM (N=3, tamaño = peor JPEG a la resolución configurada).
- `CameraActor` captura, **copia** el JPEG a un slot (anteponiendo EXIF) y devuelve el `fb`
  al driver en el acto → el driver nunca queda con buffers prestados.
- Los consumidores reciben un `FrameRef`: puntero inmutable con **conteo de referencias atómico**;
  el slot vuelve al pool cuando se destruye la última referencia (RAII). Subida, web y SD pueden
  leer el mismo frame en paralelo sin locks y sin uso-después-de-liberar.
- Si no hay slots libres, la captura se saltea (métrica `frames_dropped`) en vez de bloquear.

## 5. Flujo principal: snapshot → PrusaConnect

```
tick (1 s) ──► SnapshotScheduler.due(now)? ──no──► fin
                     │ sí
                     ▼
           CameraActor.Capture ──► FrameRef ──► ConnectUplink.Publish
                                          │            │
                                          │            ├─ 204/200 → policy.onSuccess
                                          │            ├─ 401/403 → estado "token inválido" (sin reintento)
                                          │            └─ red/TLS/5xx → Backoff exponencial con jitter
                                          └──► (opcional) timelapse en SD
```

- El intervalo sale del `trigger_scheme` que el usuario elige **en la web de PrusaConnect**
  (respuesta de `PUT /c/info`); el valor local es solo el default.
- `ConnectivityRecovery` decide escalar: *reintentar → reconectar WiFi → reiniciar*, con umbrales
  en minutos sin **ningún** éxito de red. Un fallo del backend nunca corta el WiFi por sí solo (B1).

## 6. Configuración

- Esquema tipado único (`core/domain/config.hpp`): cada campo con rango, default y si requiere
  reinicio. La API REST y la UI se validan contra el mismo esquema.
- Persistencia en **NVS** (no emulación EEPROM) con `schema_version` y migraciones explícitas.
- Secretos (clave WiFi, token, clave web) nunca salen por la API: se muestran como `"set": true`.

## 7. Seguridad (modelo de amenazas: LAN doméstica + vecinos al alcance del WiFi)

- **Primer arranque**: AP de servicio con clave **única por dispositivo** (derivada de la MAC +
  sal, impresa en el log serie) y en `192.168.4.1`; se cierra al configurar el WiFi.
- **Web**: login con usuario/clave → cookie de sesión `HttpOnly; SameSite=Strict`. Toda
  mutación es `POST`/`PUT` con `Content-Type: application/json` (CSRF bloqueado por SameSite +
  preflight). Clientes automáticos usan `Authorization: Bearer <api-token>`.
- **Sin endpoints anónimos** salvo `/login` y los assets estáticos de la pantalla de login.
- **OTA**: `POST /api/v1/ota` autenticado; imagen verificada por firma (sin quemar eFuses) y
  **rollback automático** si la nueva versión no confirma salud en 60 s.
- **TLS**: bundle de CAs de ESP-IDF (cubre la transición de Let's Encrypt, S7).
- UI sin `innerHTML` con datos: todo por `textContent` (XSS por construcción, S4).

## 8. Observabilidad

- `GET /api/v1/status`: uptime, heap interno/PSRAM (actual y mínimo), RSSI, subidas OK/fallidas,
  último error, motivo de los últimos 8 reinicios (en NVS), versión.
- Log en **buffer circular en RAM** (visible en la web al instante) + volcado a SD por lotes
  (cada 10 s o 4 KB), con rotación que **borra** los archivos viejos (F6).
- Partición `coredump`: si hay un panic, el siguiente arranque lo reporta y se descarga por la API.

## 9. Web UI

- Estética Prusa (tokens de color/tipografía de la UI original, ver `web/src/styles.css`).
- Vanilla JS con módulos ES, sin jQuery ni frameworks (sin build de Node); se sirve gzip desde
  flash con `Cache-Control` por hash de contenido.
- Pantallas: Snapshot/estado · Cámara · PrusaConnect · Red · Sistema (logs, OTA, reinicios).

## 10. Estructura del repo

```
firmware-byclaude/
  docs/ARCHITECTURE.md, docs/adr/        decisiones
  firmware/                              proyecto ESP-IDF
    main/                                composition root
    components/core/                     núcleo portable (domain, app, ports, protocol)
    components/adapters/…                implementaciones ESP-IDF (se agregan por corte)
    boards/ai_thinker.hpp                pines y capacidades
    partitions.csv, sdkconfig.defaults
  web/src/                               UI (HTML/CSS/JS)
  test/host/                             tests del núcleo en la Mac (GoogleTest)
  tools/                                 scripts (embed web, flash, etc.)
```

## 11. Estrategia de pruebas

1. **Host (Mac)**: `core` completo con GoogleTest + fakes; corre en segundos, en cada commit.
2. **Integración**: un fake de PrusaConnect en Python en la Mac; la cámara real apunta a él
   (hostname configurable) para probar errores 401/503/timeouts reproducibles.
3. **En hardware**: soak de 48 h contra PrusaConnect real, midiendo `/api/v1/status`.

## 12. Plan de cortes verticales

1. **Snapshot → PrusaConnect** con config por Kconfig/NVS, métricas y logs. *(mínimo útil)*
2. Web: login, estado, snapshot, configuración de cámara/Connect/red.
3. OTA con rollback + coredump.
4. SD: logs persistentes, timelapse, navegador de archivos.
5. PrusaLink (subir solo mientras imprime), MQTT/Home Assistant, stream.
