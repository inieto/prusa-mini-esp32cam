# ADR-0017: Keep the last log lines in RTC memory across resets

- Status: accepted · 2026-11-10

## Context
After a crash or watchdog reset the 16 KB RAM log is gone; without an SD card nothing tells what
happened just before. A first attempt kept history in the PSRAM the CPU cannot map (himem): the
data mostly survived resets, but the PSRAM initialisation overwrote scattered words on every
boot. Survival there is undocumented and may differ per chip, so it was dropped before commit.

## Decision
- The last **6 KB of log** go to a ring in **RTC slow memory** (`RTC_NOINIT_ATTR`), which
  ESP-IDF documents as preserved across software resets, panics and watchdogs. It is written
  from the same log hook as the RAM ring, inside its critical section, without allocation.
- At boot the previous contents are taken **only if** the reset was not a power-on (after a
  power-on, which includes the board's reset button, RTC memory holds random bits) and the ring's
  header passes an integrity check; control bytes from an interrupted write are scrubbed. The
  ring then starts empty for the new run.
- `GET /api/v1/logs/previous` serves it; *System* shows it next to the reason of the last reset.
- The ring logic (`core::LogTail`) is pure and host-tested.

## Consequences
+ A crash or watchdog leaves its last ~80 lines readable from the web, with no SD card.
− Not after a power cut or the reset button; 6 KB of the 8 KB RTC slow memory are used.
