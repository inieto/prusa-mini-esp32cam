# ADR-0003: Single-owner actors and a reference-counted FramePool

- Status: accepted · 2026-09-27

## Context
Bugs B4, B7–B10 in v1.1.2: semaphores never released, the driver's frame buffer read by the web
server while another task was returning it, the sensor reconfigured in parallel with a capture.

## Decision
Each resource (camera, network, SD, Connect client) belongs to one task that processes a message
queue. Frames are copied into a `FramePool` of slots preallocated in PSRAM and shared as immutable
`FrameRef`s with an atomic count; the driver's `fb` is returned immediately. HTTP handlers do not
call drivers: they send a message with a timeout or read the latest `FrameRef`.

## Consequences
+ No locks on the read path; a frame in use cannot be freed.
+ No heap allocation on the hot path (fixed slots).
− One extra copy per photo (~20–150 KB into PSRAM, < 5 ms): fine at one photo every 10 s or more.
