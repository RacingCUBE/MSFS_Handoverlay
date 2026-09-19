#include "SimConnectClient.h"
#include "SimConnect.h"
#include <iostream>

namespace {

// Free function, not a class method - SimConnect_CallDispatch requires an exact
// `void (CALLBACK*)(SIMCONNECT_RECV*, DWORD, void*)` signature, and keeping this out of
// the class means SimConnectClient.h never needs to include SimConnect.h itself.
void CALLBACK dispatchProc(SIMCONNECT_RECV* pData, DWORD /*cbData*/, void* pContext) {
    auto* self = static_cast<SimConnectClient*>(pContext);
    if (pData->dwID == SIMCONNECT_RECV_ID_QUIT) {
        std::cout << "[SimConnect] MSFS reported quit - disconnecting" << std::endl;
        self->disconnect();
    } else if (pData->dwID == SIMCONNECT_RECV_ID_EXCEPTION) {
        auto* ex = reinterpret_cast<SIMCONNECT_RECV_EXCEPTION*>(pData);
        std::cerr << "[SimConnect] Exception, code " << ex->dwException << std::endl;
    }
}

}  // namespace

SimConnectClient::~SimConnectClient() {
    disconnect();
}

bool SimConnectClient::connect() {
    disconnect();

    HRESULT hr = SimConnect_Open(&m_hSimConnect, "MSFSHandOverlay", nullptr, 0, 0, 0);
    if (FAILED(hr) || !m_hSimConnect) {
        m_lastError = "SimConnect_Open failed - is MSFS running and loaded into a flight?";
        m_hSimConnect = nullptr;
        return false;
    }

    m_lastError.clear();
    return true;
}

void SimConnectClient::disconnect() {
    if (m_hSimConnect) {
        SimConnect_Close(m_hSimConnect);
        m_hSimConnect = nullptr;
    }
    m_eventIds.clear();
    m_nextEventId = 1;
}

void SimConnectClient::update() {
    if (!m_hSimConnect) return;
    SimConnect_CallDispatch(m_hSimConnect, dispatchProc, this);
}

bool SimConnectClient::sendEvent(const std::string& eventName) {
    if (!m_hSimConnect) {
        m_lastError = "Not connected to MSFS";
        return false;
    }
    if (eventName.empty()) {
        return false;  // no event configured for this control - not an error, just a no-op
    }

    unsigned long eventId;
    auto it = m_eventIds.find(eventName);
    if (it != m_eventIds.end()) {
        eventId = it->second;
    } else {
        eventId = m_nextEventId++;
        HRESULT hr = SimConnect_MapClientEventToSimEvent(m_hSimConnect, eventId, eventName.c_str());
        if (FAILED(hr)) {
            m_lastError = "Failed to map SimConnect event: " + eventName;
            return false;
        }
        m_eventIds[eventName] = eventId;
    }

    HRESULT hr = SimConnect_TransmitClientEvent(m_hSimConnect, SIMCONNECT_OBJECT_ID_USER, eventId, 0,
                                                 SIMCONNECT_GROUP_PRIORITY_HIGHEST,
                                                 SIMCONNECT_EVENT_FLAG_GROUPID_IS_PRIORITY);
    if (FAILED(hr)) {
        m_lastError = "Failed to transmit SimConnect event: " + eventName;
        return false;
    }
    return true;
}
