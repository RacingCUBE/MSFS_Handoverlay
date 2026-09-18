// CapacitiveTouchZones - reference firmware for MSFSHandOverlay's capacitive touch panel,
// using an external Adafruit MPR121 breakout for touch sensing. Use this instead of
// ../CapacitiveTouchZones_ESP32/ (the board's own built-in touch pins) if you need more
// zones than one board's built-in touch channels cover, or prefer a dedicated touch IC -
// MPR121s can be chained on I2C (up to 4 via the ADDR pin) for up to 48 zones.
//
// WHAT THIS IS FOR
// ----------------
// Tell the PC *that* a touch happened and roughly *where* (a coarse zone = one HID
// button), so HandTouchTracker on the PC side (TouchInput.h/.cpp) can narrow its search
// for which virtual button it actually was, using the camera's hand-segmentation mask.
// This firmware doesn't need to know about individual virtual buttons at all - it just
// mirrors each MPR121 electrode's touched/untouched state onto the matching HID button.
//
// HARDWARE
// --------
// - An ESP32-S2 or ESP32-S3 board with its USB port wired to the native USB peripheral
//   (needed for USB HID output - see ARDUINO IDE SETUP below). This is the same
//   requirement as the built-in-touch sketch; MPR121 only changes how touch is sensed,
//   not how the result reaches the PC.
// - One or more Adafruit MPR121 12-channel capacitive touch breakouts (I2C).
// - Each MPR121 electrode pin wired to a conductive zone (copper tape/foil/mesh) placed
//   behind or around the group of virtual buttons it should cover. Zones can overlap
//   loosely with button groups - exact boundaries don't matter much since the camera does
//   final disambiguation; the zone only needs to narrow things down.
//
// ARDUINO IDE SETUP
// ------------------
// - Requires the "Adafruit MPR121" library (Library Manager -> search "MPR121").
// - Board: an ESP32-S2 or ESP32-S3 variant. Tools > USB Mode: "USB-OTG (TinyUSB)" (exact
//   wording varies by core version) to enable native USB HID.
//
// PC SIDE (TouchInput.h/.cpp): reads this as a plain USB HID gamepad via GLFW's joystick
// API - button index == zone. No serial/COM port, no custom text protocol.

#include <Wire.h>
#include "Adafruit_MPR121.h"
#include "USB.h"
#include "USBHIDGamepad.h"

Adafruit_MPR121 cap = Adafruit_MPR121();
USBHIDGamepad Gamepad;

// Bitmask of which electrodes were touched as of the last poll, used to detect state
// changes per electrode (both edges - see mirroring note in loop() below).
uint16_t lastTouched = 0;

void setup() {
  // Uncomment for debugging over the board's separate USB-serial/debug port, if it has one:
  // Serial.begin(115200);

  if (!cap.begin(0x5A)) {
    // No serial console to report to in the normal (non-debug) configuration - a fast
    // blink on the board LED would be a reasonable substitute if this sketch needs to
    // signal init failure in the field; left as a bare loop here for simplicity.
    while (true) { delay(1000); }
  }

  USB.begin();
  Gamepad.begin();
}

void loop() {
  uint16_t currTouched = cap.touched();

  if (currTouched != lastTouched) {
    for (uint8_t zone = 0; zone < 12; zone++) {
      bool wasTouchedZone = (lastTouched & (1 << zone)) != 0;
      bool isTouchedZone = (currTouched & (1 << zone)) != 0;
      if (isTouchedZone == wasTouchedZone) continue;

      // Mirror the physical touch state 1:1 (pressed for as long as actually touched)
      // rather than a brief pulse - the PC side detects the released->pressed edge itself
      // every frame, so it needs the state to still be "pressed" whenever it happens to
      // poll, not just for one instant.
      if (isTouchedZone) {
        Gamepad.pressButton(zone);
      } else {
        Gamepad.releaseButton(zone);
      }
    }
    Gamepad.send();
    lastTouched = currTouched;
  }

  // MPR121 has its own internal debounce/filtering, but a small poll delay avoids
  // flooding the USB HID link if a touch is right on the detection threshold and flickers.
  delay(10);
}
