# ADR-0004: Staged connectivity recovery

- Status: accepted · 2026-09-27 (amended by [ADR-0009](0009-fallback-setup-ap.md): the reboot is
  postponed while someone is connected to the fallback AP)

## Context
v1.1.2 disconnects WiFi on any backend failure (B1) and reboots after 60 s without WiFi (B2),
causing cascades of disconnects and reboots.

## Decision
Errors are classified: `Auth` (401/403: do not retry, report), `Client` (4xx), `Server`
(5xx/503), `Transport` (DNS/TCP/TLS/timeout). Retries use exponential backoff + jitter
(10 s → 5 min). The ladder is measured in **time without any network success, and only while the
network is failing** (server unreachable or WiFi down):
1. < 5 min: just retry.
2. ≥ 5 min and WiFi reports connected: reconnect WiFi once.
3. ≥ 30 min: reboot (recorded with its reason in NVS).
If the server answers, even with an error, the network is healthy and the ladder resets.

Note: the first version measured only time without success; a test
(`BackendErrorsBackOffButNeverTouchWifi`) showed that with a 5 min backoff on 503 it still
reconnected WiFi (v1.1.2's B1 by another route). That is why the ladder requires a "failing"
state.

## Consequences
+ A PrusaConnect problem neither degrades the local network nor reboots the camera.
− Thresholds are configurable; defaults get tuned with the 48 h soak.
