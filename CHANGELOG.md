# Changelog

Formato basado en [Keep a Changelog](https://keepachangelog.com/es-ES/1.1.0/); versiones según
[SemVer](https://semver.org/lang/es/). Referencias como B1, S4 o F2 apuntan a los hallazgos sobre
v1.1.2 (auditoría todavía no publicada en el repo).

## [Sin publicar]

### Agregado
- Servidor web en el dispositivo con la UI embebida (gzip, ~18 KB, servida con ETag).
- Login sin contraseña de fábrica: el primer usuario se crea desde la web durante los primeros
  15 minutos tras encender. PBKDF2-SHA256, cookie `HttpOnly; SameSite=Strict`, bloqueo de 30 s
  tras 5 intentos fallidos (S1, S2, S5).
- API REST `/api/v1`: estado, snapshot, luz, reinicio, logs, configuración de cámara,
  PrusaConnect y red, escaneo WiFi. Nunca devuelve el token ni contraseñas (S6).
- Diagnóstico sin cable serie: log en vivo en la web y historial de los últimos 8 reinicios con
  el tiempo que venía encendida la cámara.
- Techo de ganancia, exposición nocturna (AEC2) e intensidad/anticipo del flash configurables
  (F2: imagen oscura).
- El intervalo de fotos se toma del que se elige en la web de PrusaConnect (`trigger_scheme`).
- AP de configuración `PrusaCam-XXXX` en 192.168.4.1 con clave única por dispositivo (B12, S2).
- Núcleo portable con 46 tests en la Mac (GoogleTest + AddressSanitizer/UBSan).
- `AGENTS.md` (y `CLAUDE.md` que lo importa) con invariantes de arquitectura, comandos y
  convenciones para agentes de IA y contribuidores; índice y plantilla de ADRs.

### Cambiado
- Reescritura completa sobre ESP-IDF 5.5.5 y C++23, sin Arduino (ADR-0001).
- Un único dueño por recurso y frames con referencias contadas: desaparecen las carreras entre
  la web, el stream y las subidas (B4, B7–B10).
- TLS con el bundle de CAs de ESP-IDF y buffers en PSRAM, con conexión persistente (S7, B6).
- Un error de PrusaConnect ya no corta el WiFi: reintentos con backoff y recuperación escalonada
  solo cuando la red realmente falla (B1, B2).
- El detector de brownout queda activo y los reinicios por baja tensión quedan registrados.

### Compatibilidad
- Se reproduce el *fingerprint* de v1.1.2: con el mismo token, PrusaConnect sigue reconociendo
  la cámara migrada.

### Pendiente
- Primera prueba en hardware.
- OTA por web con rollback, coredump descargable, logs y timelapse en SD, PrusaLink.
