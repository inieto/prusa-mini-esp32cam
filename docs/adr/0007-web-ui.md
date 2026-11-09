# ADR-0007: Vanilla web UI with the Prusa look, embedded gzipped

- Status: accepted · 2026-09-27

## Decision
HTML + CSS + JS with ES modules, no jQuery, no frameworks, no Node in the build. Visual tokens
taken from Prusa's original UI (orange `#FA6831`, greys `#2A2A2A/#797979/#F5F5F5`, outlined
buttons that turn orange on hover, navigation underlined in orange). Embedded gzip-compressed via
`EMBED_FILES`. Data always rendered with `textContent`.

## Consequences
+ ~20 KB of flash (jQuery alone was 87 KB). + No JS build chain to maintain.
− UI components written by hand (there are few).
− Icons/styles derived from a GPL-3.0 project → this project is GPL-3.0. The Prusa logo is a
  registered trademark: personal use; replace it if distributed.
