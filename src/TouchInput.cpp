#include "TouchInput.h"
#include <sstream>
#include <iostream>

TouchInput::~TouchInput() {
    disconnect();
}

bool TouchInput::connect(const std::string& comPort, int baudRate) {
    disconnect();  // safe no-op if not already connected

    // Serial ports must be opened via "\\.\COMn" (not just "COMn") for the Win32 API to
    // handle port numbers >= 10 correctly; the prefix is harmless for lower numbers too.
    std::string path = "\\\\.\\" + comPort;

    m_hSerial = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                             OPEN_EXISTING, 0, nullptr);
    if (m_hSerial == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        m_lastError = "Failed to open " + comPort + " (Win32 error " + std::to_string(err) + ")";
        return false;
    }

    DCB dcb = {};
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(m_hSerial, &dcb)) {
        m_lastError = "GetCommState failed on " + comPort;
        CloseHandle(m_hSerial);
        m_hSerial = INVALID_HANDLE_VALUE;
        return false;
    }
    dcb.BaudRate = static_cast<DWORD>(baudRate);
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;  // Most Arduino/ESP32 boards reset on DTR toggle;
                                             // keeping it enabled avoids repeated resets.
    if (!SetCommState(m_hSerial, &dcb)) {
        m_lastError = "SetCommState failed on " + comPort;
        CloseHandle(m_hSerial);
        m_hSerial = INVALID_HANDLE_VALUE;
        return false;
    }

    // Short read timeout so the background thread can notice m_running went false and exit
    // promptly on disconnect(), rather than blocking indefinitely in ReadFile.
    COMMTIMEOUTS timeouts = {};
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 100;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    SetCommTimeouts(m_hSerial, &timeouts);

    m_lastError.clear();
    m_connected.store(true, std::memory_order_release);
    m_running.store(true, std::memory_order_release);
    m_readThread = std::make_unique<std::thread>(&TouchInput::readThreadFunc, this);
    return true;
}

void TouchInput::disconnect() {
    m_running.store(false, std::memory_order_release);
    if (m_readThread) {
        if (m_readThread->joinable()) {
            m_readThread->join();
        }
        m_readThread.reset();
    }
    if (m_hSerial != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hSerial);
        m_hSerial = INVALID_HANDLE_VALUE;
    }
    m_connected.store(false, std::memory_order_release);

    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_pendingEvents.clear();
}

bool TouchInput::pollEvent(TouchEvent& outEvent) {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    if (m_pendingEvents.empty()) return false;
    outEvent = m_pendingEvents.back();  // most recent only - see header comment
    m_pendingEvents.clear();
    return true;
}

bool TouchInput::parseLine(const std::string& line, TouchEvent& outEvent) {
    std::istringstream iss(line);
    std::string tag;
    int zone = 0;
    uint32_t millis = 0;
    if (!(iss >> tag)) return false;
    if (tag != "TOUCH") return false;  // e.g. firmware boot banner, debug prints - not an error
    if (!(iss >> zone)) return false;
    iss >> millis;  // optional trailing field; a missing one just leaves millis at 0

    outEvent.zone = zone;
    outEvent.deviceMillis = millis;
    outEvent.receivedAt = std::chrono::steady_clock::now();
    return true;
}

void TouchInput::readThreadFunc() {
    std::string lineBuffer;
    char chunk[256];

    while (m_running.load(std::memory_order_acquire)) {
        DWORD bytesRead = 0;
        BOOL ok = ReadFile(m_hSerial, chunk, sizeof(chunk), &bytesRead, nullptr);
        if (!ok) {
            // Port went away (device unplugged, etc.) - stop cleanly rather than spinning
            // on a permanently failing ReadFile.
            m_lastError = "ReadFile failed - device disconnected?";
            m_connected.store(false, std::memory_order_release);
            break;
        }
        if (bytesRead == 0) {
            continue;  // read timeout with no data - normal, just means nothing arrived yet
        }

        for (DWORD i = 0; i < bytesRead; ++i) {
            char c = chunk[i];
            if (c == '\n') {
                if (!lineBuffer.empty() && lineBuffer.back() == '\r') {
                    lineBuffer.pop_back();
                }
                TouchEvent evt;
                if (parseLine(lineBuffer, evt)) {
                    std::lock_guard<std::mutex> lock(m_queueMutex);
                    m_pendingEvents.push_back(evt);
                }
                lineBuffer.clear();
            } else {
                lineBuffer.push_back(c);
                if (lineBuffer.size() > 128) {
                    // Runaway line (noise on the wire, wrong baud rate, etc.) - drop it
                    // rather than growing unbounded.
                    lineBuffer.clear();
                }
            }
        }
    }
}
