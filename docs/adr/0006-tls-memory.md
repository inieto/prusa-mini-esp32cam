# ADR-0006: mbedTLS en PSRAM y conexión persistente

- Estado: aceptada · 2026-09-27

## Contexto
Con v1.1.2 las subidas fallaban con `-32512 SSL Memory allocation failed` al bajar la RAM
interna (~30 KB libres).

## Decisión
`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y` y buffers dinámicos; `esp_http_client` con keep-alive
hacia PrusaConnect (una sesión TLS reutilizada); nunca dos sesiones TLS simultáneas (el chequeo
de actualizaciones se serializa con las subidas).

## Consecuencias
+ La RAM interna queda para WiFi/LwIP/stacks. − Handshake algo más lento en PSRAM (irrelevante
con keep-alive).
