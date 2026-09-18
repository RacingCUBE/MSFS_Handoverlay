#pragma once

#include <string>
#include <vector>
#include <chrono>

// One touch event derived from a rising edge on a HID gamepad button - see the TouchInput
// class comment for why a HID gamepad rather than a serial/COM-port link. "zone" is just
// the gamepad button index; the capacitive touch microcontroller owns the mapping from its
// physical touch pins to button indices, this side doesn't need to know how it got there.
struct TouchEvent {
    int zone = 0;
    std::chrono::steady_clock::time_point receivedAt;  // Local PC time the edge was noticed -
                                                         // this is what gets correlated
                                                         // against camera frame timing.
};

// Reads a capacitive-touch microcontroller that presents itself as a USB HID gamepad, one
// button per touch zone (see arduino/CapacitiveTouchZones_ESP32/ - an ESP32-S2/S3 can both
// sense its own touch pins AND emit a native USB HID report, so no separate USB-serial
// chip, COM port, or custom text protocol is needed). GLFW already enumerates USB HID
// gamepads/joysticks generically, so this class is just edge-detection on top of
// glfwGetJoystickButtons() - the same pattern main.cpp already uses for its joystick reset
// button, reused here instead of introducing a second, different input mechanism.
//
// IMPORTANT: update() must be called once per frame from the main/window thread, after
// glfwPollEvents() - GLFW joystick state is only valid when read from that thread. There is
// no background thread here (unlike an earlier serial-based version of this class) - a HID
// gamepad doesn't need one, since GLFW already polls it as part of its own event pump.
class TouchInput {
public:
    // Finds the joystick at the given GLFW joystick ID (GLFW_JOYSTICK_1..GLFW_JOYSTICK_LAST),
    // or auto-detects the first present joystick if joystickID is -1, and resets
    // edge-detection state. Returns false and sets getLastError() if no joystick is found.
    // Safe to call again later (e.g. from the UI) to switch or retry.
    bool connect(int joystickID);

    // Clears the connection. Safe to call even if not connected.
    void disconnect();

    bool isConnected() const { return m_joystickID >= 0; }
    int getJoystickID() const { return m_joystickID; }

    // Call once per frame (main thread, after glfwPollEvents()). Diffs the gamepad's
    // current button state against last frame's and queues a TouchEvent for each button
    // that just transitioned released -> pressed. A no-op if not connected.
    void update();

    // Pops the most recent pending event into outEvent. Returns false if none pending. If
    // more than one button transitioned since the last poll (shouldn't normally happen at
    // human touch rates), older ones are dropped - only "where is the hand right now"
    // matters for matching.
    bool pollEvent(TouchEvent& outEvent);

    // Returns true if the given button/zone index is currently held down, as of the most
    // recent update() call. Separate from pollEvent()'s "just pressed" edge events - used
    // for continuous dial-rotation tracking while a touch stays held (see main.cpp), where
    // holding a dial doesn't generate new edge events on its own.
    bool isHeld(int zone) const;

    const std::string& getLastError() const { return m_lastError; }

private:
    int m_joystickID = -1;
    std::vector<unsigned char> m_lastButtonState;
    std::vector<TouchEvent> m_pendingEvents;
    std::string m_lastError;
};
