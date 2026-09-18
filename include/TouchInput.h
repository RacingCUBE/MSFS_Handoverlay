#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <memory>
#include <chrono>
#include <cstdint>
#include <windows.h>

// One touch event received from the capacitive touch panel's microcontroller over a
// serial (USB virtual COM) connection. The panel itself only knows "something touched
// zone N" - it can't tell which specific virtual button under that zone was pressed.
// The zone exists purely to narrow which calibrated buttons to consider when matching
// against the fingertip position found in the camera's segmentation mask (see
// HandTouchTracker.h); a "zone" can be as coarse as "the whole panel is one zone"
// (zone always 0) if the hardware can't distinguish regions at all - matching still
// works, just with a larger search space per event.
struct TouchEvent {
    int zone = 0;
    std::chrono::steady_clock::time_point receivedAt;  // Local PC time the event was parsed,
                                                         // NOT the microcontroller's own clock -
                                                         // this is what gets correlated against
                                                         // camera frame timing in main.cpp.
    uint32_t deviceMillis = 0;  // Microcontroller's own millis() at detection - informational
                                 // only (clock drift/latency vs. receivedAt is not corrected for).
};

// Reads a tiny text line protocol off a serial port from a capacitive-touch sensing
// microcontroller (Arduino/ESP32 + MPR121 or similar - see arduino/CapacitiveTouchZones/
// for a reference firmware). Expected line format, one per touch event:
//
//     TOUCH <zone> <millis>\n
//
// (ASCII, newline-terminated, CR optional). Unrecognized lines are silently ignored rather
// than treated as errors, so firmware debug prints (e.g. "READY", "ERR ...") don't kill the
// connection.
//
// connect()/disconnect() are meant to be called from the UI thread (e.g. the Touch
// Calibration tab's Connect/Disconnect buttons). pollEvent() is the only method meant to be
// called every frame from the main loop - it's a cheap mutex-protected queue pop when empty,
// which is the common case between touches.
class TouchInput {
public:
    TouchInput() = default;
    ~TouchInput();

    // Opens the given COM port (e.g. "COM5") at the given baud rate and starts the
    // background reader thread. Returns false and sets getLastError() on failure - most
    // commonly the port doesn't exist or is already open elsewhere.
    bool connect(const std::string& comPort, int baudRate);

    // Stops the reader thread and closes the port. Safe to call even if not connected.
    void disconnect();

    bool isConnected() const { return m_connected.load(std::memory_order_acquire); }

    // Pops the most recent pending event into outEvent and clears the queue. Returns false
    // if no event is pending. If more than one event arrived since the last poll (shouldn't
    // normally happen at human touch rates, but a bouncing sensor could do it), older ones
    // are dropped - only "where is the hand right now" matters for matching.
    bool pollEvent(TouchEvent& outEvent);

    const std::string& getLastError() const { return m_lastError; }

private:
    HANDLE m_hSerial = INVALID_HANDLE_VALUE;
    std::atomic<bool> m_connected{false};
    std::atomic<bool> m_running{false};
    std::unique_ptr<std::thread> m_readThread;

    std::mutex m_queueMutex;
    std::vector<TouchEvent> m_pendingEvents;

    std::string m_lastError;

    void readThreadFunc();
    static bool parseLine(const std::string& line, TouchEvent& outEvent);
};
