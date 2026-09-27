# ADR-0007: UI web vanilla con estética Prusa, embebida gzip

- Estado: aceptada · 2026-09-27

## Decisión
HTML + CSS + JS con módulos ES, sin jQuery ni frameworks ni Node en el build. Tokens visuales
tomados de la UI original de Prusa (naranja `#FA6831`, grises `#2A2A2A/#797979/#F5F5F5`,
botones con borde que se vuelven naranja en hover, navegación subrayada en naranja). Se embebe
comprimida con gzip vía `EMBED_FILES`. Datos siempre con `textContent`.

## Consecuencias
+ ~20 KB en flash (jQuery solo pesaba 87 KB). + Sin cadena de build JS que mantener.
− Componentes UI escritos a mano (pocos).
− Íconos/estilos derivados de un proyecto GPL-3.0 → este proyecto es GPL-3.0. El logo de Prusa
  es marca registrada: uso personal; reemplazar si se distribuye.
