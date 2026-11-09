# ADR-0011: SD card actor — batched logs with rotation, timelapse, file browser

- Status: accepted · 2026-11-09

## Context
The log lives in a 16 KB RAM ring: a crash or reboot loses the history needed to explain it.
v1.1.2 wrote every line straight to the card (slow, wears it) and never rotated its log file
(F6). A timelapse per print was one of the reasons to have a camera on the printer at all.
On the AI Thinker board the SD slot shares pins: DATA1 is the flash LED (GPIO4) and DATA2 the
flash-voltage strap (GPIO12).

## Decision
- **1-bit SDMMC** (CLK 14, CMD 15, DATA0 2): the LED and the strap stay free. FAT with long file
  names; never formatted by the firmware. Mount is retried every minute while no card is found,
  and a write error unmounts (card pulled out) until the next retry.
- **One storage task owns every write** (ADR-0003). It wakes on a timelapse frame or every
  10 s. Logs: the RAM ring is read incrementally (byte cursor) and appended to
  `/logs/YYYY-MM-DD.log`; on start-up and at day change only the newest 14 files are kept.
- **Timelapse**: `off`, `always` (one folder per day) or `printing` (one folder per print, named
  by date, time and file name; needs PrusaLink, ADR-0012). Frames come from the regular snapshot
  schedule through a `FrameObserver` port: no extra captures, and recording continues while
  PrusaConnect is unreachable. The hand-over is a single-slot mailbox holding a `FrameRef`
  (newest wins); frames are only saved once wall time is known, named `HHMMSS.jpg` (UTC).
- **File browser** in the API (`sd`, `sd/list`, `sd/file`, `sd/delete`): paths are decoded and
  sanitised in the core (no `..`, no absolute paths). Reads and deletes run in the HTTP task —
  FATFS serialises access per volume, and contending with a 10-second log append is harmless.
  Only JPEGs open inline; everything else downloads (the card can never serve HTML).
- All path, naming, rotation and timelapse rules are pure core code with host tests.

## Consequences
+ Logs survive reboots and crashes (up to the last 10 s); the card is written in batches.
+ Per-print timelapses ready for `ffmpeg -pattern_type glob`.
− 1-bit mode is slower (~half the 4-bit throughput): irrelevant for one JPEG every ≥ 10 s.
− Frames before NTP sync are not recorded.
