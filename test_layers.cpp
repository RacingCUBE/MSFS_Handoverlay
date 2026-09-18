#include <cstdio>
#include <cstdlib>
#include <windows.h>

// Dynamically load openxr_loader to enumerate layers
typedef enum XrResult {
    XR_SUCCESS = 0
} XrResult;

typedef enum XrStructureType {
    XR_TYPE_API_LAYER_PROPERTIES = 6
} XrStructureType;

typedef struct XrApiLayerProperties {
    XrStructureType type;
    void* next;
    char layerName[256];
    uint32_t specVersion;
    uint32_t layerVersion;
    char description[256];
} XrApiLayerProperties;

typedef XrResult (*PFN_xrEnumerateApiLayerProperties)(uint32_t, uint32_t*, XrApiLayerProperties*);

int main() {
    printf("=== OpenXR Layer Enumeration Test ===\n\n");

    // Try loading the OpenXR loader
    HMODULE loader = LoadLibraryA("openxr_loader.dll");
    if (!loader) {
        // Try vcpkg path
        loader = LoadLibraryA("C:\\vcpkg\\installed\\x64-windows\\bin\\openxr_loader.dll");
    }
    if (!loader) {
        // Try build directory
        loader = LoadLibraryA("C:\\Users\\T4A-4\\source\\repos\\MSFSHandOverlay\\MSFSHandOverlay\\MSFSHandOverlay\\build\\lib\\Release\\openxr_loader.dll");
    }
    if (!loader) {
        printf("ERROR: Could not load openxr_loader.dll\n");
        return 1;
    }
    printf("Loaded openxr_loader.dll\n");

    auto enumLayers = (PFN_xrEnumerateApiLayerProperties)GetProcAddress(loader, "xrEnumerateApiLayerProperties");
    if (!enumLayers) {
        printf("ERROR: Could not find xrEnumerateApiLayerProperties\n");
        FreeLibrary(loader);
        return 1;
    }

    // Get layer count
    uint32_t layerCount = 0;
    XrResult result = enumLayers(0, &layerCount, nullptr);
    printf("xrEnumerateApiLayerProperties returned: %d\n", result);
    printf("Layer count: %u\n\n", layerCount);

    if (layerCount > 0) {
        XrApiLayerProperties* layers = (XrApiLayerProperties*)calloc(layerCount, sizeof(XrApiLayerProperties));
        for (uint32_t i = 0; i < layerCount; i++) {
            layers[i].type = XR_TYPE_API_LAYER_PROPERTIES;
        }

        result = enumLayers(layerCount, &layerCount, layers);
        printf("Layers found:\n");
        for (uint32_t i = 0; i < layerCount; i++) {
            printf("  [%u] %s\n       %s\n", i, layers[i].layerName, layers[i].description);
        }
        free(layers);
    } else {
        printf("No layers found!\n");
    }

    // Check for our DllMain log
    if (GetFileAttributesA("C:\\Temp\\MSFSHandOverlay_DllMain.log") != INVALID_FILE_ATTRIBUTES) {
        printf("\n>>> DllMain log EXISTS - our DLL was loaded!\n");
    } else {
        printf("\n>>> No DllMain log - our DLL was NOT loaded\n");
    }

    FreeLibrary(loader);
    return 0;
}
