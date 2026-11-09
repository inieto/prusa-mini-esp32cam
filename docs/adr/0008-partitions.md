# ADR-0008: Partition table (4 MB)

- Status: accepted · 2026-09-27

| Name | Type | Offset | Size |
|---|---|---|---|
| nvs | data/nvs | 0x9000 | 24 KB |
| otadata | data/ota | 0xF000 | 8 KB |
| phy_init | data/phy | 0x11000 | 4 KB |
| ota_0 | app | 0x20000 | 1.875 MB |
| ota_1 | app | 0x200000 | 1.875 MB |
| coredump | data/coredump | 0x3E0000 | 64 KB |

No `factory` partition: the first flash goes to `ota_0` and updates alternate, with rollback.
