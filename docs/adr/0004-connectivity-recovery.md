# ADR-0004: Recuperación de conectividad escalonada

- Estado: aceptada · 2026-09-27

## Contexto
v1.1.2 desconecta el WiFi ante cualquier fallo del backend (B1) y reinicia tras 60 s sin WiFi
(B2), generando cascadas de cortes y reinicios.

## Decisión
Errores clasificados: `Auth` (401/403: no reintentar, avisar), `Client` (4xx), `Server`
(5xx/503), `Transport` (DNS/TCP/TLS/timeout). Reintentos con backoff exponencial + jitter
(10 s → 5 min). Escalera medida en **tiempo sin ningún éxito de red, y solo mientras la red
está fallando** (servidor inalcanzable o WiFi caído):
1. < 5 min: solo reintentar.
2. ≥ 5 min y WiFi reporta conectado: reconectar WiFi una vez.
3. ≥ 30 min: reinicio (registrado con motivo en NVS).
Si el servidor contesta, aunque sea con error, la red está sana y la escalera se reinicia.

Nota: la primera versión medía solo el tiempo sin éxito; un test (`BackendErrorsBackOffButNeverTouchWifi`)
mostró que con backoff de 5 min ante 503 reconectaba el WiFi igual (el B1 de v1.1.2 por otra vía).
Por eso la escalera exige estado "fallando".

## Consecuencias
+ Un problema de PrusaConnect no degrada la red local ni reinicia la cámara.
− Umbrales configurables; los defaults se ajustan con el soak de 48 h.
