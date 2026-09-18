# Capacitive Touch -> Virtual Button Matching

## Why

The physical panel's buttons are currently wired straight into SimConnect via real
microswitches. The goal here is to remove that wiring: a coarse capacitive-touch sensor
says *that* a touch happened (and roughly where, via a "zone"), and the fingertip position
computed from the existing AI hand-segmentation mask (`SegmentationEngine`) at that exact
instant says *which specific virtual button* it was. Vision alone can't reliably tell
"touching" from "hovering near"; the touch event supplies that missing confirmation with
real contact timing, so the camera only has to answer "where", not "whether".

**Current scope**: matching and logging/visualizing the result in the UI. It does **not**
fire SimConnect events yet - that's the natural next step once matching accuracy is
verified against the real panel.

## Pieces

- `arduino/CapacitiveTouchZones/` - reference firmware (Arduino + Adafruit MPR121) that
  sends `TOUCH <zone> <millis>\n` over serial on each touch's rising edge. Hardware not
  built yet; any microcontroller speaking this protocol works.
- `include/TouchInput.h` / `src/TouchInput.cpp` - Win32 serial reader, background thread,
  parses the line protocol into `TouchEvent{zone, receivedAt}`.
- `include/HandTouchTracker.h` / `src/HandTouchTracker.cpp` -
  - `findFingertipNormalized()`: given one eye's alpha mask, finds the largest hand
    contour and returns the point on it farthest from the edge the arm enters from (a
    fingertip, not a plain centroid - the earlier design note flagged that a mask
    centroid is too coarse for closely-packed buttons).
  - `matchButton()`: nearest calibrated button to that position, gated by eye and
    (loosely) by zone, rejecting matches beyond a configurable max distance.
- `Config.h`/`Config.cpp` - `TouchConfig` (enable flag, COM port, baud, entry edge, max
  match distance) and `TouchButtonCalibration` (name, zone, eye, normalized x/y),
  persisted under `[Touch]` / `[TouchButtonN]` sections in `settings.ini`.
- `main.cpp` - "Touch Calibration" UI tab; per-frame handling right after the AI
  segmentation masks are computed each frame.

## Calibration workflow (once hardware exists)

1. Chroma Key tab -> switch to **AI Segmentation** mode (touch matching needs a real
   alpha mask; it's a no-op under legacy Chroma Key).
2. Touch Calibration tab -> set the COM port/baud the microcontroller enumerates as,
   click **Connect**.
3. Set **Arm entry edge** to whichever side of the camera frame the hand/arm physically
   enters from (this rig: overhead cameras, so probably Bottom).
4. **Add New Button**, name it, then click its **Capture** button (choose **Eye: L/R**
   first if the button sits closer to one eye's camera).
5. Physically touch the real button on the panel. The next touch event's detected
   fingertip position is written into that calibration entry, along with whatever zone
   the touch reported.
6. Repeat for every button, then **Save to Config File** at the bottom of the window.

## Verifying a match

The tab shows the most recent touch event's outcome for a few seconds: which button
matched (or "no match") plus the eye and normalized fingertip coordinates - this is the
"visualize" half of current scope, alongside the same info logged to
`C:\Temp\MSFSHandOverlay_App.log` (stdout/stderr are redirected there for the whole app).

## Known limitations / next steps

- No SimConnect wiring yet - matches are informational only.
- No auto-reconnect if the microcontroller's COM port drops mid-session; use the
  Disconnect/Connect buttons.
- `findFingertipNormalized()`'s "farthest point from the entry edge" heuristic assumes a
  single hand/arm silhouette per eye and a fixed, known entry direction; it will pick a
  wrong point if two hands are in frame at once.
- Button names typed in **Add New Button** aren't editable afterward (delete and re-add
  to rename) - kept simple deliberately for this first pass.
