#pragma once

#include <d3d11.h>
#include <string>

// Simplified VROverlay for API layer approach
// Main app writes to shared memory, API layer handles actual VR rendering
class VROverlay {
public:
    VROverlay();
    ~VROverlay();

    bool initialize(const std::string& appName);
    void shutdown();

    // Write stereo frame to shared memory (API layer reads it)
    bool updateTextureD3D11(ID3D11Texture2D* texture);

    // Write raw BGR camera frames directly to shared memory (bypasses CPU chroma key).
    // By default the API layer's GPU shader computes chroma-key alpha itself. Passing
    // non-null leftAlpha/rightAlpha (single-channel, same width/height/row stride as the
    // BGR frames) instead writes those precomputed alpha values directly and tells the
    // API layer to use them as-is (ML segmentation mode) rather than re-deriving alpha
    // from chroma-key math.
    bool writeRawStereoFrame(const uint8_t* leftBGR, const uint8_t* rightBGR,
                             int width, int height, int stride,
                             const uint8_t* leftAlpha = nullptr, int leftAlphaStride = 0,
                             const uint8_t* rightAlpha = nullptr, int rightAlphaStride = 0);

    bool setOpacity(float opacity);
    bool show();
    bool hide();

    // Update overlay position in shared memory
    void resetPosition();

    // Draw a crosshair on the current frame in shared memory (for pin-point calibration)
    void drawCrosshair(float u, float v);

    // Legacy compatibility stubs (not used in API layer mode)
    bool setOverlayTransform(float width, float height, float distance, float horizOffset, float vertOffset) {
        return true;
    }
    void updateWorldPosition() { /* Not used */ }
    float getInitialHeadYaw() const { return 0.0f; }
    float getCurrentHeadYaw() const { return 0.0f; }
    float getYawDelta() const { return 0.0f; }
    float getCompensationYaw() const { return 0.0f; }
    float getInitialHeadPitch() const { return 0.0f; }
    float getCurrentHeadPitch() const { return 0.0f; }
    float getPitchDelta() const { return 0.0f; }
    float getCompensationPitch() const { return 0.0f; }

    bool isInitialized() const { return m_initialized; }

private:
    bool m_initialized = false;
    bool m_visible = false;
};
