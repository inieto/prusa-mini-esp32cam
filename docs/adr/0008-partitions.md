# ADR-0008: Tabla de particiones (4 MB)

- Estado: aceptada · 2026-09-27

| Nombre | Tipo | Offset | Tamaño |
|---|---|---|---|
| nvs | data/nvs | 0x9000 | 24 KB |
| otadata | data/ota | 0xF000 | 8 KB |
| phy_init | data/phy | 0x11000 | 4 KB |
| ota_0 | app | 0x20000 | 1,875 MB |
| ota_1 | app | 0x200000 | 1,875 MB |
| coredump | data/coredump | 0x3E0000 | 64 KB |

Sin partición `factory`: el primer flash va a `ota_0` y las actualizaciones alternan con rollback.
