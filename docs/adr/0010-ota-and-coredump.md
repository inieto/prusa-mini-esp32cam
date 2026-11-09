# ADR-0010: Web OTA with health-checked rollback, unsigned images; crash dumps over the API

- Status: accepted · 2026-11-09 (amends [ADR-0005](0005-security-defaults.md): images are not
  signed for now)

## Context
Updating required a serial cable and someone at the printer. `BOOTLOADER_APP_ROLLBACK_ENABLE`
and a `coredump` partition were already in place (ADR-0008) but unused. ADR-0005 planned
signature-verified images (`SECURE_SIGNED_APPS_NO_SECURE_BOOT`), which needs a private signing
key on every machine that builds a release; losing it blocks web updates until the next serial
flash.

## Decision
- `POST /api/v1/ota`, authenticated, raw body as `application/octet-stream` (forces a CORS
  preflight, so with `SameSite=Strict` no other site can push firmware through a logged-in
  browser). The image streams into the inactive slot with sequential erase; nothing is buffered
  whole.
- Before writing, the image header is checked: ESP32 application, **same project name**. That
  stops flashing e.g. the v1.1.2 firmware, which runs "fine" and could never roll back here.
  `esp_ota_end` verifies the SHA-256 of the whole image.
- **Rollback**: the new image boots *pending verify* and confirms itself after **60 s of uptime
  with the station connected** (or with no WiFi configured). Any reset before that — crash,
  watchdog, brownout, the recovery ladder's reboot — makes the bootloader restore the previous
  slot. The UI shows "on probation" and, after a rollback, which version failed.
- **No signing for now**: authenticated upload + project check + SHA-256. Signing can be enabled
  later without changing the API (a new ADR when keys are managed).
- **Crash dumps**: `GET /api/v1/status` reports a stored dump (task, PC, panic reason);
  `GET /api/v1/coredump` downloads the ELF, `POST /api/v1/coredump/erase` clears it. Decoding
  happens on a computer with the matching build (`idf.py coredump-info`).

## Consequences
+ Updates and crash analysis without a cable; a bad update costs at most one reboot.
− Someone with the web password can install any build of this project, including an older one.
− A legitimate update installed while WiFi is down stays on probation; a power cut before it
  comes online reverts it (it can simply be installed again).
