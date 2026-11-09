# ADR-0009: Fallback setup AP when the configured WiFi is unreachable

- Status: accepted · 2026-11-09

## Context
On board #2 the station failed to join the home network after a reflash (`AUTH_EXPIRE`, reason
0x200: the router never answered, typically weak signal or marginal power). The camera kept
retrying blindly; the only way to see what was going on, or to fix the credentials, was a serial
cable. ADR-0005 only raised the setup AP when no WiFi was configured, and ADR-0004 reboots after
30 min of failing network, which would also kick off anyone trying to fix it.

## Decision
- After **3 minutes** without an IP, the device raises the usual setup AP `PrusaCam-XXXX`
  (same per-device password, 192.168.4.1, channel 6, at most 2 clients) in **APSTA** mode.
- While the fallback AP is up, the station keeps retrying **every 30 s** instead of immediately:
  each attempt scans channels and would disconnect phones attached to the AP.
- Once the station gets an IP, the AP is closed as soon as it has no clients (checked every
  30 s), so whoever is using it is not cut off mid-configuration.
- The reboot of ADR-0004 (`ConnectivityLost`) is **postponed while a client is connected to the
  AP**; the postponement is logged at most once a minute.
- Diagnostics: the disconnect reason is logged by name on the first failure and every 10th
  after that; RSSI is logged on every connection, with a warning below −75 dBm.

Rejected: rebooting into a pure AP mode (loses the chance to reconnect on its own when the router
comes back); raising the AP immediately on the first failure (a router reboot would expose the
AP for no reason).

## Consequences
+ A camera that cannot reach its WiFi is reachable and fixable without a serial cable.
+ Weak-signal problems show up in the log (RSSI, reason names) instead of being guessed.
− The setup AP can be visible to neighbours while the WiFi is down; it keeps the per-device
  password and the 15-minute user-creation window of ADR-0005.
− Retrying every 30 s instead of immediately adds up to 30 s to reconnection while the AP is up.
