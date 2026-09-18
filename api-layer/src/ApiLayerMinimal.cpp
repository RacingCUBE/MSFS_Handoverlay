// MINIMAL TEST LAYER - Does nothing except pass through
// This is to test if the OpenXR loader can handle ANY layer from us

#define XR_USE_GRAPHICS_API_D3D11
#include <d3d11.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openxr/openxr_loader_negotiation.h>
#include <windows.h>

// Global next layer function pointer
static PFN_xrGetInstanceProcAddr g_nextGetInstanceProcAddr = nullptr;

// DllMain for load detection
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        HANDLE hFile = CreateFileA("C:\\Temp\\MinimalLayer_DllMain.log", FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            const char* msg = "=== MINIMAL LAYER DLL LOADED ===\r\n";
            DWORD written;
            WriteFile(hFile, msg, (DWORD)strlen(msg), &written, NULL);
            CloseHandle(hFile);
        }
    }
    return TRUE;
}

// Our xrGetInstanceProcAddr - just pass everything through
static XRAPI_ATTR XrResult XRAPI_CALL MinimalXrGetInstanceProcAddr(
    XrInstance instance,
    const char* name,
    PFN_xrVoidFunction* function) {

    static bool firstCall = true;
    if (firstCall) {
        HANDLE hFile = CreateFileA("C:\\Temp\\MinimalLayer_GetProcAddr.log", FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            const char* msg = ">>> FIRST GetProcAddr CALL\r\n";
            DWORD written;
            WriteFile(hFile, msg, (DWORD)strlen(msg), &written, NULL);
            CloseHandle(hFile);
        }
        firstCall = false;
    }

    // Just pass through to next layer
    if (!g_nextGetInstanceProcAddr) {
        return XR_ERROR_HANDLE_INVALID;
    }
    return g_nextGetInstanceProcAddr(instance, name, function);
}

// Negotiate with loader
extern "C" {

__declspec(dllexport) XRAPI_ATTR XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(
    const XrNegotiateLoaderInfo* loaderInfo,
    const char* layerName,
    XrNegotiateApiLayerRequest* apiLayerRequest) {

    HANDLE hFile = CreateFileA("C:\\Temp\\MinimalLayer_Negotiate.log", FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        const char* msg = ">>> NEGOTIATE START\r\n";
        DWORD written;
        WriteFile(hFile, msg, (DWORD)strlen(msg), &written, NULL);
        CloseHandle(hFile);
    }

    // Validate parameters
    if (!loaderInfo || !apiLayerRequest) {
        return XR_ERROR_INITIALIZATION_FAILED;
    }

    // Validate loader info
    if (loaderInfo->structType != XR_LOADER_INTERFACE_STRUCT_LOADER_INFO ||
        loaderInfo->structVersion != XR_LOADER_INFO_STRUCT_VERSION ||
        loaderInfo->structSize != sizeof(XrNegotiateLoaderInfo)) {
        return XR_ERROR_INITIALIZATION_FAILED;
    }

    // Validate API layer request
    if (apiLayerRequest->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST ||
        apiLayerRequest->structVersion != XR_API_LAYER_INFO_STRUCT_VERSION ||
        apiLayerRequest->structSize != sizeof(XrNegotiateApiLayerRequest)) {
        return XR_ERROR_INITIALIZATION_FAILED;
    }

    // Set up our layer interface
    apiLayerRequest->layerInterfaceVersion = XR_CURRENT_API_VERSION;
    apiLayerRequest->layerApiVersion = XR_CURRENT_API_VERSION;

    // Note: To properly get nextGetInstanceProcAddr, we need xrCreateApiLayerInstance
    // For this minimal test layer, we'll just set it to nullptr
    g_nextGetInstanceProcAddr = nullptr;

    // Provide our xrGetInstanceProcAddr
    apiLayerRequest->getInstanceProcAddr = MinimalXrGetInstanceProcAddr;
    apiLayerRequest->createApiLayerInstance = nullptr;

    hFile = CreateFileA("C:\\Temp\\MinimalLayer_Negotiate.log", FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        const char* msg = "<<< NEGOTIATE SUCCESS\r\n";
        DWORD written;
        WriteFile(hFile, msg, (DWORD)strlen(msg), &written, NULL);
        CloseHandle(hFile);
    }

    return XR_SUCCESS;
}

} // extern "C"
