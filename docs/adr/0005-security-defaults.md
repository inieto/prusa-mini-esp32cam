# ADR-0005: Seguridad por defecto

- Estado: aceptada · 2026-09-27

## Decisión
- AP de servicio con clave por dispositivo y en 192.168.4.1; solo mientras no hay WiFi configurado
  o por acción explícita (botón/serial).
- Sin contraseña de fábrica: el primer usuario se crea desde la UI y solo durante los primeros
  15 minutos tras el arranque (evita que alguien de la LAN "reclame" la cámara después).
  Contraseña con PBKDF2-HMAC-SHA256 (4096 iteraciones, sal de 16 bytes del RNG por hardware);
  5 intentos fallidos bloquean el login 30 s.
- Web con login y cookie de sesión `HttpOnly; SameSite=Strict`; mutaciones solo por
  `POST/PUT` JSON; API token Bearer para automatización. Ningún endpoint anónimo con datos.
- OTA autenticada, verificada por firma (`SECURE_SIGNED_APPS_NO_SECURE_BOOT`) con rollback.
- **No** se habilita Secure Boot ni Flash Encryption: queman eFuses de forma irreversible y un
  error deja la placa inservible; el modelo de amenazas (LAN doméstica) no lo justifica.
- Bundle de CAs de ESP-IDF para TLS.

## Consecuencias
+ Cierra S1–S7. − Acceso físico por serie sigue permitiendo reflashear (aceptado).
