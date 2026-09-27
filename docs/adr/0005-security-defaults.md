# ADR-0005: Seguridad por defecto

- Estado: aceptada · 2026-09-27

## Decisión
- AP de servicio con clave por dispositivo y en 192.168.4.1; solo mientras no hay WiFi configurado
  o por acción explícita (botón/serial).
- Web con login y cookie de sesión `HttpOnly; SameSite=Strict`; mutaciones solo por
  `POST/PUT` JSON; API token Bearer para automatización. Ningún endpoint anónimo con datos.
- OTA autenticada, verificada por firma (`SECURE_SIGNED_APPS_NO_SECURE_BOOT`) con rollback.
- **No** se habilita Secure Boot ni Flash Encryption: queman eFuses de forma irreversible y un
  error deja la placa inservible; el modelo de amenazas (LAN doméstica) no lo justifica.
- Bundle de CAs de ESP-IDF para TLS.

## Consecuencias
+ Cierra S1–S7. − Acceso físico por serie sigue permitiendo reflashear (aceptado).
