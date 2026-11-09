# ADR-0005: Secure by default

- Status: accepted · 2026-09-27 (amended by [ADR-0009](0009-fallback-setup-ap.md): the setup AP
  also comes up when the configured WiFi is unreachable for 3 minutes)

## Decision
- Service AP with a per-device password at 192.168.4.1; only while no WiFi is configured or on
  explicit action (button/serial).
- No factory password: the first user is created from the UI and only during the first
  15 minutes after boot (stops someone on the LAN from "claiming" the camera later).
  Password stored with PBKDF2-HMAC-SHA256 (4096 iterations, 16-byte salt from the hardware RNG);
  5 failed attempts lock the login for 30 s.
- Web login with an `HttpOnly; SameSite=Strict` session cookie; mutations only via JSON
  `POST/PUT`; Bearer API token for automation. No anonymous endpoint exposes data.
- Authenticated OTA, signature-verified (`SECURE_SIGNED_APPS_NO_SECURE_BOOT`), with rollback.
- Secure Boot and Flash Encryption are **not** enabled: they burn eFuses irreversibly and a
  mistake bricks the board; the threat model (home LAN) does not justify them.
- ESP-IDF CA bundle for TLS.

## Consequences
+ Closes S1–S7. − Physical serial access still allows reflashing (accepted).
