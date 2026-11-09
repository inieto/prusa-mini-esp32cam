# Architecture decision records (ADR)

One decision per file, numbered and never deleted: if a decision changes, a new ADR supersedes
it. Template: [`0000-template.md`](0000-template.md).

| ADR | Decision | Status |
|---|---|---|
| [0001](0001-esp-idf-cpp23.md) | ESP-IDF v5.5.5 + C++23, no Arduino | accepted |
| [0002](0002-hexagonal-core.md) | Portable hexagonal core, without ESP-IDF | accepted |
| [0003](0003-actors-framepool.md) | Single-owner actors and a reference-counted FramePool | accepted |
| [0004](0004-connectivity-recovery.md) | Staged connectivity recovery | accepted (amended by 0009) |
| [0005](0005-security-defaults.md) | Secure by default | accepted (amended by 0009, 0010) |
| [0006](0006-tls-memory.md) | mbedTLS in PSRAM and a persistent connection | accepted |
| [0007](0007-web-ui.md) | Vanilla web UI with the Prusa look, embedded gzipped | accepted |
| [0008](0008-partitions.md) | Partition table (4 MB) | accepted |
| [0009](0009-fallback-setup-ap.md) | Fallback setup AP when the configured WiFi is unreachable | accepted |
| [0010](0010-ota-and-coredump.md) | Web OTA with health-checked rollback, unsigned images; crash dumps over the API | accepted |
| [0011](0011-sd-card.md) | SD card actor: batched logs with rotation, timelapse, file browser | accepted |
