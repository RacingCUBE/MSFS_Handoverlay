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

#include "USB.h"
#include "USBHIDGamepad.h"

USBHIDGamepad Gamepad;

const uint8_t touchPins[] = { 1, 2, 3, 4, 5, 6, 7, 8 };  // Edit to your actual wiring/zone count
const int numZones = sizeof(touchPins) / sizeof(touchPins[0]);

// Raw touchRead() values vary a lot by board, trace length, and how large/conductive each
// zone's copper area is - there's no universal correct number here. Find yours by
// uncommenting the raw-value printout in setup() below (over the board's separate
// USB-serial/debug port, if it has one, or a temporary Serial.begin() here) and watching
// values with each zone untouched vs. touched.
const uint16_t touchThreshold = 40000;

bool wasTouched[16] = { false };  // sized for the max S2/S3 channel count

void setup() {
  // Uncomment for raw-value calibration (see THRESHOLD note above):
  // Serial.begin(115200);
  // for (int i = 0; i < numZones; i++) Serial.printf("zone %d raw=%u\n", i, touchRead(touchPins[i]));

  USB.begin();
  Gamepad.begin();
}

void loop() {
  for (int zone = 0; zone < numZones; zone++) {
    uint16_t raw = touchRead(touchPins[zone]);
    bool isTouched = raw > touchThreshold;

    if (isTouched != wasTouched[zone]) {
      // Mirror the physical touch state 1:1 (pressed for as long as actually touched)
      // rather than a brief pulse - the PC side detects the released->pressed edge itself
      // every frame, so it needs the state to still be "pressed" whenever it happens to
      // poll, not just for one instant.
      if (isTouched) {
        Gamepad.pressButton(zone);
      } else {
        Gamepad.releaseButton(zone);
      }
      wasTouched[zone] = isTouched;
    }
  }
  Gamepad.send();

  delay(10);
}
