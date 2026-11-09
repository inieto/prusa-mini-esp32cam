# ADR-0002: Portable hexagonal core (core without ESP-IDF)

- Status: accepted · 2026-09-27

## Decision
`components/core` holds the domain, use cases, ports (abstract interfaces) and protocols, and
**includes no ESP-IDF/FreeRTOS headers**. Adapters implement the ports. Dynamic polymorphism is
used only at the ports (≈ one virtual call per I/O operation, negligible cost); inside the core,
values and pure functions.

## Consequences
+ All behaviour (policies, retries, protocol, EXIF, validation) is tested on the Mac.
+ Changing a destination or a board does not touch the core.
− Resist "abstracting just in case": a new port only when there is a second real implementer
  (a test fake counts).
