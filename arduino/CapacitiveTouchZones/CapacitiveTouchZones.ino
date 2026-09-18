// CapacitiveTouchZones - reference firmware for MSFSHandOverlay's capacitive touch panel.
//
// WHAT THIS IS FOR
// ----------------
// The instrument panel's virtual buttons are matched to a real fingertip position computed
// from the overhead cameras' AI hand-segmentation mask (see HandTouchTracker.h on the PC
// side). This firmware's only job is telling the PC *that* a touch happened and roughly
// *where* (which coarse "zone"), so the PC can narrow its search - it deliberately does NOT
// need one electrode per virtual button. A handful of zones (e.g. one per quadrant of the
// panel, or even one whole-panel zone if wiring finer zones isn't practical) is enough; the
// camera does the fine-grained disambiguation.
//
// HARDWARE
// --------
// - Any Arduino-compatible board with a USB-serial connection (Uno, Nano, ESP32, etc.)
// - One or more Adafruit MPR121 12-channel capacitive touch breakouts (I2C). A single
//   MPR121 gives up to 12 zones; MPR121s can be chained on the same I2C bus (up to 4, via
//   the ADDR pin strapping) for up to 48 zones if the panel is large enough to need it.
// - Each MPR121 electrode pin wired to a conductive zone (copper tape/foil/mesh) placed
//   behind or around the group of virtual buttons it should cover. Zones can overlap
//   loosely with button groups - exact boundaries don't matter much since the camera does
//   final disambiguation; the zone only needs to narrow things down.
//
// Requires the "Adafruit MPR121" library (Arduino Library Manager -> search "MPR121").
//
// PROTOCOL (PC side: see TouchInput.h/.cpp)
// ------------------------------------------
// One ASCII line per touch, newline-terminated:
//     TOUCH <zone> <millis>\n
// <zone>   = 0-based electrode/zone index that went from untouched -> touched.
// <millis> = this board's own millis() at the moment of detection (informational only -
//            the PC uses its own arrival time for matching, not this value).
// Release events are NOT sent - only the leading edge of a touch matters here, since the
// camera position at that instant is what gets matched.
//
// Any other line the PC might read (e.g. this sketch's own startup banner) is ignored by
// the PC side as long as it doesn't start with "TOUCH ", so debug prints below are safe to
// leave in.

#include <Wire.h>
#include "Adafruit_MPR121.h"

Adafruit_MPR121 cap = Adafruit_MPR121();

// Bitmask of which electrodes were touched as of the last poll, used to detect the
// untouched->touched transition (rising edge) per electrode.
uint16_t lastTouched = 0;

void setup() {
  Serial.begin(115200);
  while (!Serial) { /* wait for USB CDC on boards that need it (e.g. Leonardo/ESP32-S2) */ }

  if (!cap.begin(0x5A)) {
    Serial.println("ERR MPR121 not found - check wiring/I2C address");
    while (true) { delay(1000); }
  }

  Serial.println("READY CapacitiveTouchZones");
}

void loop() {
  uint16_t currTouched = cap.touched();

  for (uint8_t zone = 0; zone < 12; zone++) {
    bool wasTouched = (lastTouched & (1 << zone)) != 0;
    bool isTouched = (currTouched & (1 << zone)) != 0;

    if (isTouched && !wasTouched) {
      Serial.print("TOUCH ");
      Serial.print(zone);
      Serial.print(" ");
      Serial.println(millis());
    }
  }

  lastTouched = currTouched;

  // MPR121 has its own internal debounce/filtering, but a small poll delay avoids
  // flooding the serial line if a touch is right on the detection threshold and flickers.
  delay(10);
}
