# ADR-0015: Digital zoom and pan through the sensor's windowing

- Status: accepted · 2026-11-10

## Context
The camera sits in a printed case on the printer; small changes in the case or mount leave the
bed off-centre or too small in the frame. The user asked for a way to "zoom in and choose a
portion" of the image digitally and send that to PrusaConnect.

## Decision
- Zoom and pan are done by the **sensor's own windowing**, not by cropping the JPEG on the ESP32:
  the sensor reads only the chosen region of its pixel array and scales it to the output size.
  - Cropping a decoded JPEG would cost ~1 MB of RAM and 1–2 s of CPU per frame, recompress (lose
    quality) and only enlarge pixels already captured.
  - With windowing a zoomed 800×600 frame still uses real sensor pixels (the OV3660 has
    2048×1536), costs nothing per frame and applies to snapshots, live view and timelapse alike.
- Settings `zoom_pct` (100–400), `pan_x` and `pan_y` (−100…100 % of the free travel). The core
  computes the window (`framing_window`): it keeps the output aspect ratio, **never upscales**
  (`max_zoom_pct`: e.g. 2.56× at 800×600 on the OV3660, 2× on the OV2640) and maps the pan the
  user sees through EXIF rotation and sensor mirroring. Host-tested.
- The adapter programs the window with `set_res_raw`: on the OV3660 with the border and timing
  values esp32-camera uses for its standard sizes (binning when the window is at least twice the
  output); on the OV2640 through its full-resolution readout and DSP window. Returning to 1×
  restores the driver's frame size. Other sensors report a maximum zoom of 1×.
- The web UI applies framing sliders immediately (no Save), so it can be tuned while watching
  the live view.

## Consequences
+ Re-centring and zooming without reprinting the case, with no image-quality or CPU cost.
− The maximum zoom is bounded by sensor resolution ÷ output resolution.
− Register semantics are sensor-specific: verified on the OV3660 (board #2: zoom up to 2.25×, pan directions confirmed); the OV2640 path
  follows the driver's own windowing but is not yet verified on hardware.
