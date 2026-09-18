#pragma once

#include <Windows.h>
#include <cstdint>

namespace MSFSHandOverlay {
namespace IPC {

// Shared memory layout for stereo camera frames
struct FrameHeader {
    uint32_t frameSequence;      // Incrementing frame counter
    uint32_t width;              // Frame width (e.g., 1280 for side-by-side)
    uint32_t height;             // Frame height (e.g., 480)
    uint32_t stride;             // Bytes per row
    uint32_t format;             // DXGI_FORMAT (e.g., DXGI_FORMAT_R8G8B8A8_UNORM)
    bool frameReady;             // True when new frame is available

    // Overlay configuration (from config file)
    float posX, posY, posZ;      // Position in LOCAL space
    float pitch, yaw, roll;      // Rotation in degrees
    float width_m, height_m;     // Physical dimensions in meters
    float opacity;               // Opacity (0-1)

    // Per-eye chroma key settings (from config, used by API layer shader)
    float chromaLeftHueCenter;   // Hue center (0-360)
    float chromaLeftHueRange;    // Hue tolerance (0-180)
    float chromaLeftSatMin;      // Min saturation to key (0-1)
    float chromaLeftValMin;      // Min brightness to key (0-1)
    float chromaLeftEdgeSoftness;// Edge smoothing (0-0.2)

    float chromaRightHueCenter;
    float chromaRightHueRange;
    float chromaRightSatMin;
    float chromaRightValMin;
    float chromaRightEdgeSoftness;

    // Border mask settings
    float borderMaskSize;            // Size of gradient mask at edges (0.0-0.5)
    float borderMaskSoftness;        // Softness of gradient transition

    // Chroma key mode
    int inverseMode;                 // 0 = remove background, 1 = keep only matched color (hands)

    // Hand brightness (software dimming for night flying)
    float handBrightness;            // 0.0 = black, 1.0 = normal brightness

    // 0 = chroma-key computed in the API layer's shader (legacy), 1 = alpha already
    // precomputed by ML segmentation and baked into the frame's own alpha channel.
    int alphaMode;
};

// Shared memory name (Local namespace - no admin rights required)
constexpr const char* SHARED_MEMORY_NAME = "Local\\MSFSHandOverlay_StereoFrames";

// Total shared memory size (header + stereo frame data)
// Side-by-side 1280x480 RGBA = 1280 * 480 * 4 = 2,457,600 bytes
constexpr size_t FRAME_DATA_SIZE = 1280 * 480 * 4;  // Max frame size
constexpr size_t SHARED_MEMORY_SIZE = sizeof(FrameHeader) + FRAME_DATA_SIZE;

// Helper class for shared memory access
class SharedFrameBuffer {
public:
    SharedFrameBuffer();
    ~SharedFrameBuffer();

    // Create shared memory (called by main app)
    bool createSharedMemory();

    // Open existing shared memory (called by API layer)
    bool openSharedMemory();

    // Get pointers
    FrameHeader* getHeader() { return m_header; }
    void* getFrameData() { return m_frameData; }

    // Check if initialized
    bool isValid() const { return m_mappedMemory != nullptr; }

private:
    HANDLE m_fileMapping = nullptr;
    void* m_mappedMemory = nullptr;
    FrameHeader* m_header = nullptr;
    void* m_frameData = nullptr;
};

} // namespace IPC
} // namespace MSFSHandOverlay
