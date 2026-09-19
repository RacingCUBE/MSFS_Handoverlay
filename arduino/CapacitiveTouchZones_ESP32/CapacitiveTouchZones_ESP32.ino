// CapacitiveTouchZones_ESP32 - reference firmware for MSFSHandOverlay's capacitive touch
// panel, using the ESP32-S2/S3's BUILT-IN capacitive touch sensing pins AND its native USB
// HID support - no external touch IC, no USB-serial chip, no COM port or custom protocol
// at all. The board just shows up as a USB gamepad with one button per touch zone.
//
// WHAT THIS IS FOR
// ----------------
// Tell the PC *that* a touch happened and roughly *where* (a coarse zone = one HID
// button), so HandTouchTracker on the PC side (TouchInput.h/.cpp) can narrow its search
// for which virtual button it actually was, using the camera's hand-segmentation mask.
// This firmware doesn't need to know about individual virtual buttons at all - it just
// mirrors each touch pin's raw touched/untouched state onto the matching HID button.
//
// HARDWARE
// --------
// - An ESP32-S2 or ESP32-S3 board with its USB port wired to the native USB peripheral
//   (check your board: some dev boards expose two USB ports - one native USB, one via a
//   separate USB-serial chip for flashing/logs only - native USB HID requires the former).
// - Each touch-capable GPIO wired to a conductive zone (copper tape/foil/mesh) placed
//   behind or around the group of virtual buttons it should cover. S2/S3 boards expose up
//   to 14 touch channels (T1-T14) depending on the specific board/pinout - check your
//   board's pinout diagram and edit touchPins[] below to match your actual wiring.
//
// ARDUINO IDE SETUP
// ------------------
// - Board: an ESP32-S2 or ESP32-S3 variant (Tools > Board > esp32 > ...).
// - Tools > USB Mode: "USB-OTG (TinyUSB)" (exact wording varies by core version) - this is
//   what enables native USB HID instead of the plain USB-serial-only mode.
// - No extra library needed beyond the ESP32 board package itself (USB.h / USBHIDGamepad.h
//   ship with it) - but API details below have shifted between core versions, so check
//   yours against the installed "ESP32 Gamepad" / "USB HID" examples if send()/press
//   button signatures don't match exactly.
//
// THRESHOLD DIRECTION - IMPORTANT
// --------------------------------
// On ESP32-S2/S3, touchRead() INCREASES when a pin is touched (higher = more capacitance
// detected) - the OPPOSITE of the original/plain ESP32, where it decreases. This sketch is
// only correct for S2/S3; double-check before reusing threshold logic from plain-ESP32
// example code.
//
// BUILT-IN LED - Espressif reference boards (Saola-1 / DevKitC-1 / DevKitM-1)
// -----------------------------------------------------------------------------
// Lights the onboard LED whenever any zone is currently touched, for a quick physical
// "yes, contact registered" check without needing a PC connected at all. Espressif's own
// ESP32-S2 boards don't have a plain on/off LED - they have a single addressable WS2812
// RGB LED, normally on GPIO18. neopixelWrite()/RGB_BUILTIN (Arduino-ESP32 core 2.0.x+) is
// the built-in way to drive it, no extra library needed. If your installed core version
// doesn't define RGB_BUILTIN, the fallback below targets GPIO18 directly, which is the
// standard pin on these boards - check your board's schematic if that's not lit correctly.

#ifndef RGB_BUILTIN
#define RGB_BUILTIN 18  // Standard onboard WS2812 pin on ESP32-S2-Saola-1 / DevKitC-1 / DevKitM-1
#endif

#include "USB.h"
#include "USBHIDGamepad.h"

USBHIDGamepad Gamepad;

// ESP32-S2's touch peripheral only covers GPIO1-GPIO14 (T1-T14) - GPIO16 and most other
// pins aren't wired to it at all, regardless of firmware. Single zone on GPIO1 (T1) here;
// add more entries (any GPIO1-14) if more zones are needed later.
const uint8_t touchPins[] = { 1 };
const int numZones = sizeof(touchPins) / sizeof(touchPins[0]);

// Raw touchRead() values vary a lot by board, trace length, and how large/conductive each
// zone's copper area is - there's no universal correct fixed number, and a wrong guess
// looks exactly like "inverted" behavior (LED off when touched) if the idle/untouched
// reading on your specific board already happens to sit above whatever constant was
// guessed. Self-calibrated against each pin's own idle reading at boot instead (see
// setup() below) - keep your hands off the pads while the board resets/powers up.
uint32_t touchBaseline[16] = { 0 };  // idle reading per pin, set once in setup()
// How far above its own idle baseline a pin's reading must rise to count as touched.
// A percentage (not a fixed count) since raw magnitudes vary a lot by board/wiring, but
// a real touch is a large relative jump either way - if this proves too sensitive or
// not sensitive enough on your hardware, adjust this one number rather than re-guessing
// a whole fixed threshold.
const float touchMarginPercent = 0.15f;  // 15% above idle baseline counts as touched

bool wasTouched[16] = { false };  // sized for the max S2/S3 channel count

void setup() {
  // Uncomment to watch raw values / baselines directly (see calibration note above):
  // Serial.begin(115200);

  neopixelWrite(RGB_BUILTIN, 0, 0, 0);  // off - no pinMode() needed, neopixelWrite handles setup

  // Self-calibrate: average several idle readings per pin right after boot. Assumes
  // nothing is touching any pad during this window - keep hands clear while the board
  // resets or powers up.
  const int kCalibrationSamples = 16;
  for (int zone = 0; zone < numZones; zone++) {
    uint32_t sum = 0;
    for (int i = 0; i < kCalibrationSamples; i++) {
      sum += touchRead(touchPins[zone]);
      delay(5);
    }
    touchBaseline[zone] = sum / kCalibrationSamples;
    // Serial.printf("zone %d idle baseline=%u\n", zone, touchBaseline[zone]);
  }

  USB.begin();
  Gamepad.begin();
}

void loop() {
  bool anyTouched = false;

  for (int zone = 0; zone < numZones; zone++) {
    uint32_t raw = touchRead(touchPins[zone]);  // touch_value_t is uint32_t on S2/S3 -
                                                  // must match exactly, a narrower type
                                                  // here silently truncates/wraps and can
                                                  // invert the apparent touch direction
    uint32_t threshold = touchBaseline[zone] + static_cast<uint32_t>(touchBaseline[zone] * touchMarginPercent);
    bool isTouched = raw > threshold;
    if (isTouched) anyTouched = true;

    if (isTouched != wasTouched[zone]) {
      // Mirror the physical touch state 1:1 (pressed for as long as actually touched)
      // rather than a brief pulse - the PC side detects the released->pressed edge itself
      // every frame, so it needs the state to still be "pressed" whenever it happens to
      // poll, not just for one instant.
      if (isTouched) {
        Gamepad.pressButton(zone);  // sends the HID report itself - no separate send() call
      } else {
        Gamepad.releaseButton(zone);  // ditto
      }
      wasTouched[zone] = isTouched;
    }
  }

  // Green for as long as ANY zone is touched, off otherwise - a quick "did that register
  // at all" check standing at the panel, independent of whether the PC is even connected.
  if (anyTouched) {
    neopixelWrite(RGB_BUILTIN, 0, 64, 0);  // dim green - full 255 is very bright up close
  } else {
    neopixelWrite(RGB_BUILTIN, 0, 0, 0);
  }

  delay(10);
}
