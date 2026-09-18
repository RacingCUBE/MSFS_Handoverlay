#include "VROverlay.h"
#include "Config.h"
#include "../api-layer/include/SharedMemory.h"
#include <iostream>
#include <cmath>

// Use shared memory instead of OpenXR session (API layer handles VR)
static std::unique_ptr<MSFSHandOverlay::IPC::SharedFrameBuffer> g_sharedFrames;

VROverlay::VROverlay() {
}

VROverlay::~VROverlay() {
    shutdown();
}

bool VROverlay::initialize(const std::string& appName) {
    std::cout << "[VROverlay] Initializing shared memory for API layer..." << std::endl;

    // Create shared memory for stereo frames
    g_sharedFrames = std::make_unique<MSFSHandOverlay::IPC::SharedFrameBuffer>();
    if (!g_sharedFrames->createSharedMemory()) {
        std::cerr << "[VROverlay] Failed to create shared memory!" << std::endl;
        return false;
    }

    // Initialize configuration in shared memory
    resetPosition();

    m_initialized = true;
    std::cout << "[VROverlay] Shared memory initialized - ready for API layer" << std::endl;
    return true;
}

void VROverlay::resetPosition() {
    if (!g_sharedFrames || !g_sharedFrames->isValid()) {
        return;
    }

    std::cout << "=== Updating overlay position ===" << std::endl;

    Config& config = Config::getInstance();
    auto* header = g_sharedFrames->getHeader();

    if (header) {
        // Update position and rotation in shared memory
        header->posX = config.overlay.posX;
        header->posY = config.overlay.posY;
        header->posZ = config.overlay.posZ;
        header->pitch = config.overlay.pitch;
        header->yaw = config.overlay.yaw;
        header->roll = config.overlay.roll;
        header->width_m = config.overlay.width;
        header->height_m = config.overlay.height;
        header->opacity = config.overlay.opacity;

        // Chroma key settings for API layer shader (respect inverse mode)
        header->inverseMode = config.chromaKey.inverseMode ? 1 : 0;
        if (config.chromaKey.inverseMode) {
            header->chromaLeftHueCenter = config.chromaKey.leftHueCenterInverse;
            header->chromaLeftHueRange = config.chromaKey.leftHueRangeInverse;
            header->chromaLeftSatMin = config.chromaKey.leftSaturationMinInverse;
            header->chromaLeftValMin = config.chromaKey.leftValueMinInverse;
            header->chromaLeftEdgeSoftness = config.chromaKey.leftEdgeSoftnessInverse;

            header->chromaRightHueCenter = config.chromaKey.rightHueCenterInverse;
            header->chromaRightHueRange = config.chromaKey.rightHueRangeInverse;
            header->chromaRightSatMin = config.chromaKey.rightSaturationMinInverse;
            header->chromaRightValMin = config.chromaKey.rightValueMinInverse;
            header->chromaRightEdgeSoftness = config.chromaKey.rightEdgeSoftnessInverse;
        } else {
            header->chromaLeftHueCenter = config.chromaKey.leftHueCenter;
            header->chromaLeftHueRange = config.chromaKey.leftHueRange;
            header->chromaLeftSatMin = config.chromaKey.leftSaturationMin;
            header->chromaLeftValMin = config.chromaKey.leftValueMin;
            header->chromaLeftEdgeSoftness = config.chromaKey.leftEdgeSoftness;

            header->chromaRightHueCenter = config.chromaKey.rightHueCenter;
            header->chromaRightHueRange = config.chromaKey.rightHueRange;
            header->chromaRightSatMin = config.chromaKey.rightSaturationMin;
            header->chromaRightValMin = config.chromaKey.rightValueMin;
            header->chromaRightEdgeSoftness = config.chromaKey.rightEdgeSoftness;
        }

        header->borderMaskSize = config.chromaKey.borderMaskSize;
        header->borderMaskSoftness = config.chromaKey.borderMaskSoftness;
        header->handBrightness = config.overlay.handBrightness;

        std::cout << "Position updated in shared memory:" << std::endl;
        std::cout << "  Position: (" << header->posX << ", " << header->posY << ", " << header->posZ << ")" << std::endl;
        std::cout << "  Rotation: Pitch=" << header->pitch << ", Yaw=" << header->yaw << ", Roll=" << header->roll << std::endl;
    }
}

bool VROverlay::updateTextureD3D11(ID3D11Texture2D* texture) {
    if (!m_initialized || !g_sharedFrames || !g_sharedFrames->isValid()) {
        return false;
    }

    auto* header = g_sharedFrames->getHeader();
    void* frameData = g_sharedFrames->getFrameData();

    if (!header || !frameData) {
        return false;
    }

    // Get D3D11 device context
    ID3D11Device* device = nullptr;
    texture->GetDevice(&device);
    if (!device) {
        return false;
    }

    ID3D11DeviceContext* context = nullptr;
    device->GetImmediateContext(&context);
    if (!context) {
        device->Release();
        return false;
    }

    // Get texture description
    D3D11_TEXTURE2D_DESC desc;
    texture->GetDesc(&desc);

    // Create staging texture for CPU readback (only once)
    static ID3D11Texture2D* stagingTexture = nullptr;
    static UINT lastWidth = 0, lastHeight = 0;

    if (!stagingTexture || lastWidth != desc.Width || lastHeight != desc.Height) {
        if (stagingTexture) {
            stagingTexture->Release();
        }

        D3D11_TEXTURE2D_DESC stagingDesc = desc;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags = 0;

        HRESULT hr = device->CreateTexture2D(&stagingDesc, nullptr, &stagingTexture);
        if (FAILED(hr)) {
            context->Release();
            device->Release();
            return false;
        }

        lastWidth = desc.Width;
        lastHeight = desc.Height;
    }

    // Copy from GPU texture to staging texture
    context->CopyResource(stagingTexture, texture);

    // Update header with frame info
    header->width = desc.Width;
    header->height = desc.Height;
    header->stride = desc.Width * 4;  // Assuming RGBA
    header->format = desc.Format;

    // Update chroma key settings every frame (so slider changes take effect immediately)
    Config& config = Config::getInstance();
    header->inverseMode = config.chromaKey.inverseMode ? 1 : 0;
    if (config.chromaKey.inverseMode) {
        header->chromaLeftHueCenter = config.chromaKey.leftHueCenterInverse;
        header->chromaLeftHueRange = config.chromaKey.leftHueRangeInverse;
        header->chromaLeftSatMin = config.chromaKey.leftSaturationMinInverse;
        header->chromaLeftValMin = config.chromaKey.leftValueMinInverse;
        header->chromaLeftEdgeSoftness = config.chromaKey.leftEdgeSoftnessInverse;
        header->chromaRightHueCenter = config.chromaKey.rightHueCenterInverse;
        header->chromaRightHueRange = config.chromaKey.rightHueRangeInverse;
        header->chromaRightSatMin = config.chromaKey.rightSaturationMinInverse;
        header->chromaRightValMin = config.chromaKey.rightValueMinInverse;
        header->chromaRightEdgeSoftness = config.chromaKey.rightEdgeSoftnessInverse;
    } else {
        header->chromaLeftHueCenter = config.chromaKey.leftHueCenter;
        header->chromaLeftHueRange = config.chromaKey.leftHueRange;
        header->chromaLeftSatMin = config.chromaKey.leftSaturationMin;
        header->chromaLeftValMin = config.chromaKey.leftValueMin;
        header->chromaLeftEdgeSoftness = config.chromaKey.leftEdgeSoftness;
        header->chromaRightHueCenter = config.chromaKey.rightHueCenter;
        header->chromaRightHueRange = config.chromaKey.rightHueRange;
        header->chromaRightSatMin = config.chromaKey.rightSaturationMin;
        header->chromaRightValMin = config.chromaKey.rightValueMin;
        header->chromaRightEdgeSoftness = config.chromaKey.rightEdgeSoftness;
    }

    // Map staging texture and copy to shared memory
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = context->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &mapped);

    if (SUCCEEDED(hr)) {
        // Copy texture data to shared memory
        size_t dataSize = desc.Width * desc.Height * 4;  // RGBA

        // Handle potential row padding
        if (mapped.RowPitch == desc.Width * 4) {
            // No padding, direct copy
            memcpy(frameData, mapped.pData, dataSize);
        } else {
            // Has padding, copy row by row
            uint8_t* dst = static_cast<uint8_t*>(frameData);
            uint8_t* src = static_cast<uint8_t*>(mapped.pData);
            for (UINT row = 0; row < desc.Height; ++row) {
                memcpy(dst, src, desc.Width * 4);
                dst += desc.Width * 4;
                src += mapped.RowPitch;
            }
        }

        context->Unmap(stagingTexture, 0);

        // Mark frame as ready and increment sequence
        header->frameSequence++;
        header->frameReady = true;
    }

    context->Release();
    device->Release();

    return SUCCEEDED(hr);
}

bool VROverlay::writeRawStereoFrame(const uint8_t* leftBGR, const uint8_t* rightBGR,
                                    int width, int height, int stride,
                                    const uint8_t* leftAlpha, int leftAlphaStride,
                                    const uint8_t* rightAlpha, int rightAlphaStride) {
    if (!m_initialized || !g_sharedFrames || !g_sharedFrames->isValid()) {
        return false;
    }

    auto* header = g_sharedFrames->getHeader();
    uint8_t* frameData = static_cast<uint8_t*>(g_sharedFrames->getFrameData());

    if (!header || !frameData) {
        return false;
    }

    int stereoWidth = width * 2;  // Side-by-side
    size_t dataSize = stereoWidth * height * 4;  // RGBA
    if (dataSize > MSFSHandOverlay::IPC::FRAME_DATA_SIZE) {
        return false;  // Frame too large for shared memory
    }

    // Write left + right frames side-by-side as RGBA with V+H flip
    // (matches the flip done by the OpenGL shader in renderCameraToTexture)
    int dstStride = stereoWidth * 4;  // RGBA stride for side-by-side output

    for (int y = 0; y < height; ++y) {
        // Vertical flip: read from bottom row, write to top
        int srcY = height - 1 - y;

        // Left eye: BGR source row
        const uint8_t* leftRow = leftBGR + srcY * stride;
        // Right eye: BGR source row
        const uint8_t* rightRow = rightBGR + srcY * stride;
        // Alpha source rows (nullptr in legacy chroma-key mode) - same vertical flip as color.
        const uint8_t* leftAlphaRow = leftAlpha ? (leftAlpha + srcY * leftAlphaStride) : nullptr;
        const uint8_t* rightAlphaRow = rightAlpha ? (rightAlpha + srcY * rightAlphaStride) : nullptr;

        // Destination row in shared memory
        uint8_t* dstRow = frameData + y * dstStride;

        // Left eye pixels (first half, horizontal flip: read right-to-left)
        for (int x = 0; x < width; ++x) {
            int srcX = width - 1 - x;  // Horizontal flip
            int srcIdx = srcX * 3;     // BGR
            int dstIdx = x * 4;        // RGBA

            dstRow[dstIdx + 0] = leftRow[srcIdx + 2];  // R (from BGR)
            dstRow[dstIdx + 1] = leftRow[srcIdx + 1];  // G
            dstRow[dstIdx + 2] = leftRow[srcIdx + 0];  // B
            // Same horizontal flip (srcX) as color, so alpha stays pixel-aligned with the hand.
            dstRow[dstIdx + 3] = leftAlphaRow ? leftAlphaRow[srcX] : 255;
        }

        // Right eye pixels (second half, horizontal flip)
        uint8_t* dstRowRight = dstRow + width * 4;
        for (int x = 0; x < width; ++x) {
            int srcX = width - 1 - x;  // Horizontal flip
            int srcIdx = srcX * 3;     // BGR
            int dstIdx = x * 4;        // RGBA

            dstRowRight[dstIdx + 0] = rightRow[srcIdx + 2];  // R
            dstRowRight[dstIdx + 1] = rightRow[srcIdx + 1];  // G
            dstRowRight[dstIdx + 2] = rightRow[srcIdx + 0];  // B
            dstRowRight[dstIdx + 3] = rightAlphaRow ? rightAlphaRow[srcX] : 255;
        }
    }

    // Update header
    header->width = stereoWidth;
    header->height = height;
    header->stride = dstStride;
    header->format = 28;  // DXGI_FORMAT_R8G8B8A8_UNORM

    // Update chroma key settings every frame (so slider changes take effect)
    Config& config = Config::getInstance();
    header->inverseMode = config.chromaKey.inverseMode ? 1 : 0;
    if (config.chromaKey.inverseMode) {
        header->chromaLeftHueCenter = config.chromaKey.leftHueCenterInverse;
        header->chromaLeftHueRange = config.chromaKey.leftHueRangeInverse;
        header->chromaLeftSatMin = config.chromaKey.leftSaturationMinInverse;
        header->chromaLeftValMin = config.chromaKey.leftValueMinInverse;
        header->chromaLeftEdgeSoftness = config.chromaKey.leftEdgeSoftnessInverse;
        header->chromaRightHueCenter = config.chromaKey.rightHueCenterInverse;
        header->chromaRightHueRange = config.chromaKey.rightHueRangeInverse;
        header->chromaRightSatMin = config.chromaKey.rightSaturationMinInverse;
        header->chromaRightValMin = config.chromaKey.rightValueMinInverse;
        header->chromaRightEdgeSoftness = config.chromaKey.rightEdgeSoftnessInverse;
    } else {
        header->chromaLeftHueCenter = config.chromaKey.leftHueCenter;
        header->chromaLeftHueRange = config.chromaKey.leftHueRange;
        header->chromaLeftSatMin = config.chromaKey.leftSaturationMin;
        header->chromaLeftValMin = config.chromaKey.leftValueMin;
        header->chromaLeftEdgeSoftness = config.chromaKey.leftEdgeSoftness;
        header->chromaRightHueCenter = config.chromaKey.rightHueCenter;
        header->chromaRightHueRange = config.chromaKey.rightHueRange;
        header->chromaRightSatMin = config.chromaKey.rightSaturationMin;
        header->chromaRightValMin = config.chromaKey.rightValueMin;
        header->chromaRightEdgeSoftness = config.chromaKey.rightEdgeSoftness;
    }

    header->borderMaskSize = config.chromaKey.borderMaskSize;
    header->borderMaskSoftness = config.chromaKey.borderMaskSoftness;

    header->handBrightness = config.overlay.handBrightness;
    header->alphaMode = (leftAlpha != nullptr) ? 1 : 0;

    // Mark frame as ready
    header->frameSequence++;
    header->frameReady = true;

    return true;
}

void VROverlay::drawCrosshair(float u, float v) {
    if (!g_sharedFrames || !g_sharedFrames->isValid()) return;
    auto* header = g_sharedFrames->getHeader();
    auto* frameData = static_cast<uint8_t*>(g_sharedFrames->getFrameData());
    if (!header || !frameData || !header->frameReady) return;

    int stereoW = (int)header->width;
    int h = (int)header->height;
    int eyeW = stereoW / 2;
    int stride = (int)header->stride;
    int cx = (int)(u * eyeW);
    int cy = (int)(v * h);
    if (cx < 0) cx = 0; if (cx >= eyeW) cx = eyeW - 1;
    if (cy < 0) cy = 0; if (cy >= h) cy = h - 1;
    int armLen = 30;

    // Draw red crosshair on both eye halves
    for (int eye = 0; eye < 2; eye++) {
        int ox = eye * eyeW + cx;
        // Horizontal arm (3 pixels thick)
        for (int dy = -1; dy <= 1; dy++) {
            int py = cy + dy;
            if (py < 0 || py >= h) continue;
            int x0 = ox - armLen; if (x0 < eye * eyeW) x0 = eye * eyeW;
            int x1 = ox + armLen; if (x1 >= (eye + 1) * eyeW) x1 = (eye + 1) * eyeW - 1;
            for (int x = x0; x <= x1; x++) {
                uint8_t* p = frameData + py * stride + x * 4;
                p[0] = 255; p[1] = 50; p[2] = 50; p[3] = 255;
            }
        }
        // Vertical arm (3 pixels thick)
        for (int dx = -1; dx <= 1; dx++) {
            int px = ox + dx;
            if (px < eye * eyeW || px >= (eye + 1) * eyeW) continue;
            int y0 = cy - armLen; if (y0 < 0) y0 = 0;
            int y1 = cy + armLen; if (y1 >= h) y1 = h - 1;
            for (int y = y0; y <= y1; y++) {
                uint8_t* p = frameData + y * stride + px * 4;
                p[0] = 255; p[1] = 50; p[2] = 50; p[3] = 255;
            }
        }
    }
}

bool VROverlay::setOpacity(float opacity) {
    if (g_sharedFrames && g_sharedFrames->isValid()) {
        auto* header = g_sharedFrames->getHeader();
        if (header) {
            header->opacity = opacity;
        }
    }
    return true;
}

bool VROverlay::show() {
    m_visible = true;
    return true;
}

bool VROverlay::hide() {
    m_visible = false;
    return true;
}

void VROverlay::shutdown() {
    if (!m_initialized) {
        return;
    }

    std::cout << "[VROverlay] Shutting down shared memory..." << std::endl;

    // Signal the API layer to stop rendering before releasing shared memory
    if (g_sharedFrames && g_sharedFrames->isValid()) {
        auto* header = g_sharedFrames->getHeader();
        if (header) {
            header->frameReady = false;
            header->frameSequence = 0;  // Reset sequence to signal shutdown
        }
    }

    g_sharedFrames.reset();
    m_initialized = false;
}
