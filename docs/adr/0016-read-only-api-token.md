# ADR-0016: Read-only API token for integrations

- Status: accepted · 2026-11-10 (implements the API token planned in
  [ADR-0005](0005-security-defaults.md), narrowed to read-only)

## Context
Integrations such as Home Assistant camera URLs, Obico or VLC need the snapshot or the stream
without a browser session: they cannot log in, and many cannot send custom headers. ADR-0005
foresaw a Bearer token but did not define its scope.

## Decision
- One token, generated from *System* with the hardware RNG (128 bits, hex). It is shown once;
  only its SHA-256 is stored (NVS `web/api_hash`), so a flash dump does not reveal it.
  Generating a new one replaces the old; it can be revoked; a factory reset erases it.
- Accepted as `Authorization: Bearer <token>` or as `?token=<token>` in the URL. The query form
  exists because MJPEG viewers and camera-URL fields cannot add headers.
- **Read-only scope**: `GET status`, `GET snapshot.jpg`, `GET stream`. Settings, actions,
  firmware updates and file access still require the logged-in session. Comparison is
  constant-time; brute force is pointless against 128 random bits.

## Consequences
+ The camera can feed other systems without sharing the admin password.
− A token in a URL can end up in browser history or proxy logs: anyone holding it can watch the
  camera (but not change it). Revoke and regenerate if it leaks.
