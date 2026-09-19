#pragma once

#include <string>
#include <unordered_map>
#include <windows.h>

// Thin wrapper around the MSFS SimConnect SDK (C:\MSFS SDK\SimConnect SDK) for firing
// client events - e.g. an autopilot knob's increment/decrement event - into a running
// MSFS instance. Send-only: no telemetry/sim-variable readback is implemented, matching
// the current need (turn a detected dial tick into an actual sim action).
//
// SimConnect_Open() only succeeds while MSFS itself is running and has finished loading
// into a flight - connecting before that, or after MSFS closes, fails or disconnects and
// needs a fresh connect() call, the same connect/disconnect pattern already used for the
// touch HID gamepad elsewhere in this app.
//
// IMPORTANT CAVEAT: events like "AP_ALT_VAR_INC"/"AP_ALT_VAR_DEC" are the standard,
// long-established SimConnect events that work on MSFS's default/basic aircraft. Complex
// study-level add-on aircraft (and some of MSFS's own higher-fidelity Working Title glass
// cockpits) sometimes implement their own autopilot logic and don't respond to these
// standard events at all - that needs verifying per-aircraft, not something this wrapper
// can detect or work around (the community fix for that class of aircraft is generally a
// WASM-module-based H:Event bridge, e.g. MobiFlight's, which is a materially different
// and larger mechanism than plain SimConnect client events).
class SimConnectClient {
public:
    ~SimConnectClient();

    // Returns false and sets getLastError() on failure - most commonly means MSFS isn't
    // running yet, or hasn't finished loading into a flight.
    bool connect();
    void disconnect();
    bool isConnected() const { return m_hSimConnect != nullptr; }

    // Call once per frame (or on some regular interval) - pumps SimConnect's message
    // queue, which is how a QUIT message (MSFS closing) gets noticed and turned into a
    // clean disconnect rather than every subsequent sendEvent() silently failing into a
    // dead handle. Safe to call even when not connected (no-op).
    void update();

    // Fires a named SimConnect client event with no data parameter (e.g. "AP_ALT_VAR_INC")
    // at the user's own aircraft. Maps the event name to a client event ID on first use
    // and caches it, so callers just pass event name strings - no pre-registration step.
    // Returns false (and sets getLastError()) if not connected or the transmit call fails.
    bool sendEvent(const std::string& eventName);

    const std::string& getLastError() const { return m_lastError; }

private:
    HANDLE m_hSimConnect = nullptr;
    std::string m_lastError;

    // Event name -> the client event ID it was registered under. DWORD is unsigned long
    // on Windows; kept as that rather than pulling SimConnect.h's DWORD-based typedefs
    // into this header, so nothing here needs the SDK header at all - only the .cpp does.
    std::unordered_map<std::string, unsigned long> m_eventIds;
    unsigned long m_nextEventId = 1;
};
