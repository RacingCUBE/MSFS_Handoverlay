# Capacitive Touch -> Virtual Button Matching

## Why

The physical panel's buttons are currently wired straight into SimConnect via real
microswitches. The goal here is to remove that wiring: a coarse capacitive-touch sensor
says *that* a touch happened (and roughly where, via a "zone"), and the fingertip position
computed from the existing AI hand-segmentation mask (`SegmentationEngine`) at that exact
instant says *which specific virtual button* it was. Vision alone can't reliably tell
"touching" from "hovering near"; the touch event supplies that missing confirmation with
real contact timing, so the camera only has to answer "where", not "whether".

**Current scope**: matching, logging/visualizing, AND (new) firing real SimConnect events
per dial tick, once a Dial control has one assigned. Buttons still log/visualize only -
only dials drive SimConnect so far, since that was the first concrete need (the altitude
knob).

## Wiring a dial to an actual MSFS control (e.g. the altitude knob)

Requires the MSFS SDK installed (in-sim: enable Developer Mode under General Options,
then install the SDK from the Developer menu) - `CMakeLists.txt` expects it at
`C:\MSFS SDK\SimConnect SDK` and fails the CMake configure step with a clear message if
it's not found there. `SimConnectClient` (`include/SimConnectClient.h`,
`src/SimConnectClient.cpp`) is a thin, send-only wrapper: `connect()`/`disconnect()`
(only succeeds while MSFS is running and loaded into a flight), `update()` (call once per
frame - pumps SimConnect's message queue so a QUIT message from MSFS closing is noticed
and disconnects cleanly), and `sendEvent(name)` (maps a named event like
`"AP_ALT_VAR_INC"` to a client event ID on first use, then transmits it at the user
aircraft).

Steps:
1. Calibrate a Dial control normally (see above).
2. Touch Calibration tab -> **MSFS connection** section -> **Connect** (MSFS must already
   be running and loaded into a flight).
3. In that dial's row, pick an entry from the **SimConnect action** dropdown - presets
   scoped to what a basic trainer (e.g. the default Cessna 152) actually has: altimeter
   baro/Kollsman, COM1/NAV1 MHz and kHz digits, the four transponder digits, and the
   elevator trim wheel. Autopilot altitude is included too, for aircraft that have one -
   the 152 doesn't. Selecting **Custom...** reveals typed Inc/Dec event fields as a
   fallback for anything not covered by the preset list.
4. **Save to Config File** to persist the mapping (`SimConnectIncEvent`/
   `SimConnectDecEvent` under that dial's `[TouchButtonN]` section).
5. Turn the dial - each tick now fires the mapped event, if SimConnect is connected and
   the fields are non-empty. Leave both fields empty to keep a dial log/visualize-only.

**Known limitation, not yet worked around**: `AP_ALT_VAR_INC`/`DEC` and similarly-aged
standard events work on MSFS's default/basic aircraft. Complex study-level add-on
aircraft (and some of MSFS's own higher-fidelity Working Title glass cockpits) sometimes
implement their own autopilot logic via custom systems that don't respond to these
standard events at all - verified per-aircraft, not something this wrapper can detect.
The community fix for that class of aircraft is generally a WASM-module-based H:Event
bridge (e.g. MobiFlight's), which is a materially larger, different mechanism than plain
SimConnect client events - not implemented here.

**Verification note on the preset event names**: `SimConnect_Open`/`MapClientEventToSimEvent`/
`TransmitClientEvent` themselves were verified directly against the installed SDK's C++
header before writing any code. The actual *event name strings* in the preset dropdown
(`KOHLSMAN_INC`, `COM_RADIO_WHOLE_INC`, `XPNDR_1000_INC`, `ELEV_TRIM_UP`, etc.) could not
be verified the same way - this SDK install doesn't ship the standard events list as
local documentation, only the connection API headers. These are well-established,
widely-referenced names from Microsoft's online SimConnect docs, but unlike the API
calls, they're not confirmed against a file on this machine - worth an eye out for a
silent no-op (event name typo'd or simply wrong) versus "aircraft doesn't support it" if
one of these doesn't do anything when tested. Note also `KOHLSMAN_INC`/`DEC`'s
well-documented misspelling: the instrument is a "Kollsman window", but Microsoft's own
event name has used this spelling since the earliest SimConnect SDKs.

**Dials, not just buttons**: several of the real controls are rotary dials, not push
buttons - touching one only confirms *that* it was touched, not which way it got turned,
since a dial's calibrated position never changes regardless of rotation. Rotation is
tracked separately, continuously, for as long as the touch stays held: not by following
the fingertip's position around the dial (a wrist twist over a small knob doesn't
necessarily sweep the fingertip through a wide arc), but by tracking the orientation of
the hand's own silhouette - i.e. detecting the twist of the hand/wrist itself, frame by
frame, and reporting the signed rotation between frames. See `computeHandOrientationAngle`
/ `angleDeltaAxis` in HandTouchTracker.

## Hardware link: USB HID gamepad, not serial

The touch microcontroller talks to the PC as a **USB HID gamepad**, one button per touch
zone - not a serial/COM port with a custom text protocol. An ESP32-S2/S3 can both sense
its own capacitive touch pins directly (`touchRead()`, up to ~14 zones with no external
parts) and present native USB HID output, so the whole link needs zero extra chips and
avoids a real problem this project already hit once: COM port letters drift on replug (see
the camera-index instability notes elsewhere in this repo), while a HID gamepad's identity
is stable. GLFW already enumerates HID gamepads/joysticks generically, so the PC side is
just edge-detection on `glfwGetJoystickButtons()` - the exact pattern `main.cpp` already
uses for its joystick reset button, reused here rather than inventing a second mechanism.

## Pieces

- `arduino/CapacitiveTouchZones_ESP32/` - reference firmware for an ESP32-S2/S3 using its
  built-in touch pins, pressing/releasing HID gamepad button N (= zone N) as each pin's
  touched state changes. This is the recommended default. Hardware not built yet.
- `arduino/CapacitiveTouchZones/` - same HID output, but reading touch via an external
  Adafruit MPR121 breakout (I2C) instead of the board's own pins - use this if more zones
  are needed than one board's built-in touch channels cover (MPR121s chain on I2C for up
  to 48 zones). Still requires an ESP32-S2/S3 for the native USB HID output.
- `include/TouchInput.h` / `src/TouchInput.cpp` - thin wrapper around
  `glfwGetJoystickButtons()`: `update()` (called once per frame from the main thread, after
  `glfwPollEvents()`) diffs button state against last frame and queues a `TouchEvent{zone,
  receivedAt}` for each button that just went released -> pressed.
- `include/HandTouchTracker.h` / `src/HandTouchTracker.cpp` -
  - `findFingertipNormalized()`: given one eye's alpha mask, finds the largest hand
    contour and returns the point on it farthest from the edge the arm enters from (a
    fingertip, not a plain centroid - the earlier design note flagged that a mask
    centroid is too coarse for closely-packed buttons).
  - `matchButton()`: nearest calibrated button/dial to that position, gated by eye and
    (loosely) by zone, rejecting matches beyond a configurable max distance.
  - `computeHandOrientationAngle()`: fits an ellipse to the hand contour and returns its
    major-axis angle - a stand-in for "which way the wrist is twisted". Only meaningful
    modulo 180 degrees (an ellipse's axis has no inherent direction), so track it with
    `angleDeltaAxis()`, not a plain subtraction, or a rotation crossing that wrap boundary
    looks like a sudden ~180 degree reversal.
- `Config.h`/`Config.cpp` - `TouchConfig` (enable flag, joystick ID, entry edge, max match
  distance) and `TouchButtonCalibration` (name, zone, eye, normalized x/y, `type`:
  Button or Dial), persisted under `[Touch]` / `[TouchButtonN]` sections in `settings.ini`.
- `main.cpp` - "Touch Calibration" UI tab; per-frame handling right after the AI
  segmentation masks are computed each frame.

## Calibration workflow (once hardware exists)

1. Chroma Key tab -> switch to **AI Segmentation** mode (touch matching needs a real
   alpha mask; it's a no-op under legacy Chroma Key).
2. Touch Calibration tab -> the microcontroller should appear under "Detected joysticks"
   once plugged in; set **Joystick ID** to its ID (or leave -1 for auto-detect if it's the
   only joystick/gamepad plugged in) and click **Connect**.
3. Set **Arm entry edge** to whichever side of the camera frame the hand/arm physically
   enters from (this rig: overhead cameras, so probably Bottom).
4. **Add New Button**, name it, set its **Type** (**Type:Btn** for a push button,
   **Type:Dial** for a rotary control - click to toggle), then click **Capture** (choose
   **Eye: L/R** first if the control sits closer to one eye's camera).
5. Physically touch the real control on the panel - for a dial, touch at/near its center.
   The next touch event's detected fingertip position is written into that calibration
   entry, along with whatever zone (HID button index) the touch reported.
6. Repeat for every control, then **Save to Config File** at the bottom of the window.

## Verifying a match

The tab shows the most recent touch event's outcome for a few seconds: which button/dial
matched (or "no match") plus the eye and normalized fingertip coordinates - this is the
"visualize" half of current scope, alongside the same info logged to
`C:\Temp\MSFSHandOverlay_App.log` (stdout/stderr are redirected there for the whole app).
For a Dial match, the log continues while the touch is held: `Dial 'X' twisted N deg
(total M deg)` for each frame's rotation past a small noise threshold, then a `released -
total rotation` summary line once the touch lifts.

## Live hand diagnostics

The Touch Calibration tab shows a continuously-updated readout (every frame, independent
of any touch event) for each eye once AI Segmentation mode is active:

- **pos**: fingertip position, normalized 0-1, (0,0) = top-left.
- **axis**: `computeHandOrientationAngle()`'s ellipse-fit angle, 0-180 degrees. This is
  what actually drives dial-rotation tracking - it's undirected (a hand at 10 degrees
  looks identical to one at 190), so it folds back on itself every half-turn on this
  readout, which is expected.
- **twist**: `computeHandDirectionAngle()`'s centroid-to-fingertip angle, a genuine +-180
  degree reading with no ambiguity - sweeps smoothly through the full range as you turn
  your wrist, so it's the more intuitive one to watch while tuning by eye. Not what dial
  tracking uses internally (see the function's own comment for why), but a much easier
  number to eyeball live.

Both go to "no hand detected"/"n/a" when segmentation doesn't see a large enough contour
(no hand in frame, or in the gap between frames RVM's recurrent state hasn't caught up).

**Axis is noisy frame-to-frame** - a **Dial angle smoothing** slider (`AxisAngleFilter`,
wrap-aware exponential smoothing) is available right above the readouts, and feeds both
this diagnostic and real dial-rotation tracking. 1.0 = no smoothing; lower values smooth
more at the cost of added lag. Tune by watching the live "axis" number while turning.

## Multi-turn dials: sessions survive a release/regrab

Real testing found that a dial needing several turns to reach a target - release, spin
the wrist back to regrip, grab again, continue - was silently losing rotation. Two
compounding causes, both fixed:

1. **Ticks used to require one frame's motion to cross the threshold on its own.** A
   brief regrab spread over just 2-3 frames could have each frame's motion individually
   too small to register, even though the total across those frames was real. Fixed with
   an accumulator (`DialSession::pendingRotationRad`): every frame's delta adds to a
   running total regardless of size, and a tick fires (with a `while`, not `if`, in case
   a single fast frame covers several detents) once the *cumulative* total crosses a full
   detent's worth, carrying the remainder forward.
2. **Tick count used to reset to zero on every fresh grab.** Releasing to reposition the
   wrist was indistinguishable from actually finishing the turn. Fixed with
   `DialSession`, keyed per calibrated dial, that persists tick count and pending
   rotation across a release - as long as the *same* dial gets touched again within
   `kDialSessionTimeoutSeconds` (3s). Touching a different dial, or coming back to the
   same one after the timeout, starts a genuinely fresh turn instead of continuing a
   stale one.

The angle-tracking baseline itself (`lastOrientationRad`, the smoothing filter) still
resets on every fresh grab, deliberately - the hand's orientation during the "let go and
reposition" phase is arbitrary and unrelated to the dial's real rotation, so carrying
that raw angle across a release would misread the repositioning motion as more turning.
Only the tick/rotation bookkeeping persists, not the angle baseline.

The Touch Calibration tab's "Active dial" section now also stays visible (greyed out,
labeled "released - grab again to continue") for the same timeout window after you let
go, instead of vanishing immediately - so the tick count doesn't look lost when the
session is actually still alive and waiting for you to continue.

**Side effect worth knowing about**: the accumulator sums *every* frame's motion toward
the tick threshold, not just frames whose own delta happens to cross it - far more
sensitive than the old per-frame-gated approach at the same threshold value. Real testing
found this alone pushed typical turns from a "-25 to +25" feel to "200+ and climbing".
`config.touch.dialTickThresholdDeg` (UI: **Degrees per tick**) exists specifically to
compensate - raised from an old effective ~1.1 degrees to a default of 8 degrees, and
live-tunable since the right value depends on real footage, not something derivable up
front.

**A related, separately-caused bug also found and fixed**: simulated touch state used to
share storage with the real device's button tracking, so clicking Connect or Disconnect
on a real joystick (even just to stop it interfering with a simulated test) silently
ended an in-progress simulated hold - `TouchInput::isHeld()` would report "not held" with
no Release ever clicked. Simulated holds now live in their own storage
(`m_simulatedHeldZones`), untouched by connect()/disconnect()/update().

## Dial rotation as tick counts, not degrees

Rather than reporting a precise rotation amount, dial tracking now counts simple +1/-1
detents: each frame the filtered axis angle moves past the noise threshold in one
direction, the active dial's tick count changes by one. This is deliberately more
forgiving of the axis signal's real-world noise and limited sensitivity (see below) than
trying to report an accurate degree amount would be - it only needs to know *which way*
things moved, not *how much*. It's also a closer match to how SimConnect knob controls
typically work anyway (discrete increment/decrement events, not a continuous angle).

The Touch Calibration tab shows an **Active dial** section with the live tick count in
large text plus a bidirectional bar (green = positive, red = negative, centered at zero,
full deflection at +-20 ticks) - meant to be readable at a glance rather than needing to
read small numbers, which is awkward to do with an HMD on. This currently only exists in
the companion app's flat window; whether it also needs to be visible inside the headset
itself (a separate change to the injected VR overlay layer, not just this app's own UI) is
still open.

## Confidence-weighted smoothing

`computeHandOrientationAngle()` also reports a per-frame confidence in [0,1], derived from
`cv::fitEllipse`'s own elongation ratio (0 = circular/no defined axis, 1 = well-elongated).
`AxisAngleFilter::update()` scales its effective smoothing weight by this confidence, so a
poorly-conditioned frame (e.g. a compact grip on a small knob) moves the filtered angle
less than a well-conditioned one at the same base smoothing setting - a frame the fit
can't really trust no longer gets treated the same as a clean one. Never fully freezes
even at confidence 0 (a small floor keeps it slowly adapting). Visible live in the tab's
diagnostics as `conf` next to the axis reading - watch this while changing grip: if it
stays low with a particular grip and higher with a flatter one, that confirms grip shape
is the actual limiting factor, not transient noise.

## Is feature-tracking worth building?

A materially different approach exists for rotation tracking: track distinct visual
points on the hand (knuckles, creases) frame-to-frame with optical flow, then estimate
rotation directly from how those points moved - unlike the ellipse fit, this doesn't
depend on the overall silhouette being elongated, so it could in principle handle a
compact/pinch grip that the current approach struggles with. But it's not a guaranteed
improvement: it needs enough distinct trackable texture to find and follow, and bare skin
under a webcam is often low on that compared to a printed marker or textured surface, and
it could suffer more than a silhouette fit does from motion blur during a fast twist. It
would also be meaningfully more code (feature detection, tracking, outlier rejection,
handling lost/regained points) - a real, different subsystem, not a tuning tweak.

Before committing to that investment, `countTrackableHandFeatures()` gives a cheap,
real-data answer: it runs `cv::goodFeaturesToTrack` on the actual hand region each frame
(diagnostic only - not used by any tracking logic) and reports the count live in the tab
as **features N**. A consistently low/unstable count across different grips and lighting
is evidence feature-tracking would trade one class of problem (poorly-conditioned
ellipse) for another (too few/unreliable tracks) rather than fixing it; a healthy, stable
count is a green light to consider actually building the rotation-estimation pipeline on
top of it.

**Known real limitation, not fixed by smoothing**: initial testing found the axis angle's
*sensitivity* can be poor, not just noisy - a 90-degree real wrist rotation moved the
reading only ~10 degrees in one test. This is consistent with an ellipse fit's angle
becoming poorly-conditioned on a compact/near-circular hand silhouette (e.g. a pinch-grip
on a small knob rather than a flat, elongated hand shape) - the fit genuinely doesn't have
a strong, well-defined axis to report in that pose. Smoothing reduces noise on whatever
signal exists; it cannot recover a signal that isn't there. If this keeps showing up
across different real grips, dial rotation may need a different underlying signal than
ellipse-fit orientation - worth revisiting once more real-hardware testing is done.

## Testing without the touch hardware

The hardest part to get right - whether the camera/segmentation pipeline correctly reads
your real hand's position and twist - doesn't actually need the touch sensor at all. The
Touch Calibration tab has a **Testing without hardware** section at the bottom:

1. Enable touch matching, switch to AI Segmentation mode, set a Zone number (any value).
2. Click **Simulate Touch (press)** - this injects a fake touch event through the exact
   same code path a real HID gamepad press would use (calibration capture, matching, and
   dial-drag start all trigger identically).
3. Put your hand wherever you want to test, or - for a Dial - keep it in view and twist
   your wrist. Watch the log / the tab's "Last touch event" readout.
4. Click **Release** to end the simulated hold (only matters for Dial testing, where the
   drag continues for as long as the zone reads as held).

This validates everything except the actual touch sensing itself: fingertip extraction,
button matching, and dial rotation direction/magnitude can all be checked against real
footage today. A spare USB gamepad, if you have one, is an alternative that also exercises
the real `TouchInput::update()` polling path (press its buttons instead of clicking
Simulate) - but the simulate button needs nothing extra at all.

## Known limitations / next steps

- No SimConnect wiring yet - matches are informational only.
- No auto-reconnect if the HID gamepad disconnects mid-session (unplugged, board reset,
  etc.); use the Disconnect/Connect buttons to re-find it.
- If another joystick/gamepad is also plugged in, auto-detect (-1) picks whichever GLFW
  enumerates first, which may not be the touch panel - set an explicit Joystick ID in that
  case rather than relying on auto-detect.
- `findFingertipNormalized()`'s "farthest point from the entry edge" heuristic assumes a
  single hand/arm silhouette per eye and a fixed, known entry direction; it will pick a
  wrong point if two hands are in frame at once.
- Dial rotation's sign convention (whether a positive `angleDeltaAxis()` result is
  clockwise or counter-clockwise as seen on screen) hasn't been verified against real
  footage - confirm it empirically once hardware exists, the same way the VR overlay's
  parallax direction was confirmed with real instrumented data rather than assumed.
- Dial tracking assumes the hand's ellipse-fit orientation changes smoothly frame to
  frame; a hand that's nearly circular in silhouette (fingers curled, seen mostly
  end-on) gives a poorly-conditioned ellipse fit and noisy angles - worth watching for
  once real footage is available.
- Button names typed in **Add New Button** aren't editable afterward (delete and re-add
  to rename) - kept simple deliberately for this first pass.
