# ADR-0006: mbedTLS in PSRAM and a persistent connection

- Status: accepted · 2026-09-27

## Context
With v1.1.2, uploads failed with `-32512 SSL Memory allocation failed` once internal RAM ran low
(~30 KB free).

## Decision
`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y` and dynamic buffers; `esp_http_client` with keep-alive to
PrusaConnect (one reused TLS session); never two TLS sessions at once (the update check is
serialized with uploads).

## Consequences
+ Internal RAM stays available for WiFi/LwIP/stacks. − Slightly slower handshake in PSRAM
(irrelevant with keep-alive).
