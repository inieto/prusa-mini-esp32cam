# ADR-0002: Núcleo hexagonal portable (core sin ESP-IDF)

- Estado: aceptada · 2026-09-27

## Decisión
`components/core` contiene dominio, casos de uso, puertos (interfaces abstractas) y protocolos,
y **no incluye headers de ESP-IDF/FreeRTOS**. Los adaptadores implementan los puertos. Se usa
polimorfismo dinámico solo en los puertos (≈ una llamada virtual por operación de I/O, costo
despreciable); dentro del núcleo, valores y funciones puras.

## Consecuencias
+ Todo el comportamiento (políticas, reintentos, protocolo, EXIF, validación) se testea en la Mac.
+ Cambiar un destino/placa no toca el núcleo.
− Hay que resistir la tentación de "abstraer por si acaso": un puerto nuevo solo cuando hay
  un segundo implementador real (fake de test cuenta).
