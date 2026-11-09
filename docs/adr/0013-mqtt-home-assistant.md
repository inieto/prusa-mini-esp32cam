# ADR-0013: MQTT bridge with Home Assistant discovery, plain MQTT only

- Status: accepted · 2026-11-09

## Context
Home Assistant is the usual place where people watch printers and automate around them
(lights, notifications when a print fails). Its MQTT integration discovers devices from
retained config messages, so no YAML is needed on the user's side.

## Decision
- `esp-mqtt` client (ESP-IDF component), configured from the *MQTT* tab: broker URI, user,
  password (a secret: never returned by the API), whether to publish images.
- Topics under `prusacam/<hostname>/`: `availability` (retained `online`, last will `offline`),
  `state` (JSON every 30 s and on changes: PrusaConnect state, light, RSSI, counters, uptime,
  printer state/progress), `image` (each scheduled capture, QoS 0), and commands
  `light/set` (`ON`/`OFF`) and `snapshot/set` (`PRESS`).
- Discovery under `homeassistant/`: a camera entity on `image`, a light switch, a snapshot
  button and sensors (PrusaConnect, printer, progress, RSSI, uploads, uptime). Node id derived
  from the MAC, so it survives renames. Payloads are built in the core and host-tested.
- A bridge task owns all publishing: frames arrive through the `FrameObserver` port into a
  one-slot mailbox (newest wins), so a slow broker only delays images, never captures.
  Commands only use the thread-safe request methods (ADR-0003).
- **Only `mqtt://`**: TLS to the broker would be a second simultaneous TLS session, which
  ADR-0006 rules out for internal RAM. Brokers for Home Assistant live on the LAN.

## Consequences
+ The camera appears in Home Assistant with a live image and controls, without configuration.
− Credentials travel in clear text on the LAN (as with most home MQTT setups).
− Two more tasks (bridge 4 KB, esp-mqtt 5 KB stacks) while MQTT is enabled.
