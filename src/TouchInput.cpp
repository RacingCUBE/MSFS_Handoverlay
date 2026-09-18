#include "TouchInput.h"
#include <GLFW/glfw3.h>

bool TouchInput::connect(int joystickID) {
    disconnect();

    if (joystickID == -1) {
        for (int i = GLFW_JOYSTICK_1; i <= GLFW_JOYSTICK_LAST; i++) {
            if (glfwJoystickPresent(i)) {
                joystickID = i;
                break;
            }
        }
    }

    if (joystickID < 0 || !glfwJoystickPresent(joystickID)) {
        m_lastError = "No joystick/HID gamepad present"
                      + (joystickID >= 0 ? (" at ID " + std::to_string(joystickID)) : std::string());
        m_joystickID = -1;
        return false;
    }

    m_joystickID = joystickID;
    int buttonCount = 0;
    const unsigned char* buttons = glfwGetJoystickButtons(m_joystickID, &buttonCount);
    m_lastButtonState.assign(buttons, buttons + buttonCount);
    m_lastError.clear();
    return true;
}

void TouchInput::disconnect() {
    m_joystickID = -1;
    m_lastButtonState.clear();
    m_pendingEvents.clear();
}

void TouchInput::update() {
    if (m_joystickID < 0) return;

    if (!glfwJoystickPresent(m_joystickID)) {
        m_lastError = "Joystick/HID gamepad disconnected";
        m_joystickID = -1;
        m_lastButtonState.clear();
        return;
    }

    int buttonCount = 0;
    const unsigned char* buttons = glfwGetJoystickButtons(m_joystickID, &buttonCount);
    if (!buttons || buttonCount == 0) return;

    if (static_cast<int>(m_lastButtonState.size()) != buttonCount) {
        // Button count changed (device re-enumerated, or this is the first read after
        // connect()) - resync silently rather than treating every button as a fresh press.
        m_lastButtonState.assign(buttons, buttons + buttonCount);
        return;
    }

    for (int i = 0; i < buttonCount; ++i) {
        bool now = (buttons[i] == GLFW_PRESS);
        bool before = (m_lastButtonState[i] == GLFW_PRESS);
        if (now && !before) {
            TouchEvent evt;
            evt.zone = i;
            evt.receivedAt = std::chrono::steady_clock::now();
            m_pendingEvents.push_back(evt);
        }
    }
    m_lastButtonState.assign(buttons, buttons + buttonCount);
}

bool TouchInput::isHeld(int zone) const {
    if (zone < 0 || zone >= static_cast<int>(m_lastButtonState.size())) return false;
    return m_lastButtonState[zone] == GLFW_PRESS;
}

bool TouchInput::pollEvent(TouchEvent& outEvent) {
    if (m_pendingEvents.empty()) return false;
    outEvent = m_pendingEvents.back();  // most recent only - see header comment
    m_pendingEvents.clear();
    return true;
}
