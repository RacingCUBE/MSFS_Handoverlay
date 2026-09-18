#pragma once

#include <string>
#include <vector>

struct CameraConfig {
    int leftCameraIndex = 0;
    int rightCameraIndex = 1;
    int frameWidth = 640;
    int frameHeight = 480;
    int fps = 30;
    int splitOffsetPixels = 0;    // Manual frame-tear split fix for the LEFT camera only, in pixels (0 = off)
    bool useValveBuiltInCamera = false;  // Capture source: false = external USB cameras, true = Valve Index built-in

    // Separate pixel offsets for each camera (since they're separate physical cameras)
    int leftPixelOffsetX = -22;   // Left camera X offset
    int leftPixelOffsetY = 18;    // Left camera Y offset
    int rightPixelOffsetX = 21;   // Right camera X offset
    int rightPixelOffsetY = 0;    // Right camera Y offset

    // Per-camera image adjustment, applied to the raw captured frame before chroma-key/
    // segmentation/display (so it affects everything downstream, not just the preview).
    // Brightness is an additive offset in pixel units; contrast is a multiplicative gain
    // applied around it (frame' = frame * contrast + brightness, saturating 0-255).
    float leftBrightness = 0.0f;
    float leftContrast = 1.0f;
    float rightBrightness = 46.0f;
    float rightContrast = 1.0f;

};

struct InputConfig {
    int joystickResetButton = 11;  // Joystick button for reset (matches settings.ini)
    int joystickID = -1;  // Joystick ID to use (-1 = auto-detect first joystick)
};

struct ChromaKeyConfig {
    // Normal mode: Left eye chroma key settings (background removal)
    float leftHueCenter = 299.5f;      // Green hue (0-360)
    float leftHueRange = 180.0f;       // Hue tolerance
    float leftSaturationMin = 0.45f;   // Minimum saturation to key out (need saturated greens)
    float leftValueMin = 0.0f;         // Minimum brightness to key out
    float leftEdgeSoftness = 0.2f;     // Smooth edges

    // Normal mode: Right eye chroma key settings (background removal)
    float rightHueCenter = 192.2f;     // Green hue (0-360)
    float rightHueRange = 117.9f;      // Hue tolerance
    float rightSaturationMin = 0.14f;  // Minimum saturation to key out
    float rightValueMin = 0.0f;        // Minimum brightness to key out
    float rightEdgeSoftness = 0.2f;    // Smooth edges

    // Inverse mode: Left eye chroma key settings (skin detection)
    float leftHueCenterInverse = 67.6f;   // Fair skin hue (~15-25 degrees)
    float leftHueRangeInverse = 24.5f;    // Skin hue tolerance
    float leftSaturationMinInverse = 0.58f; // Min saturation for skin
    float leftValueMinInverse = 0.20f;     // Min brightness for skin
    float leftEdgeSoftnessInverse = 0.15f;

    // Inverse mode: Right eye chroma key settings (skin detection)
    float rightHueCenterInverse = 20.0f;
    float rightHueRangeInverse = 25.0f;
    float rightSaturationMinInverse = 0.10f;
    float rightValueMinInverse = 0.20f;
    float rightEdgeSoftnessInverse = 0.15f;

    // Inverse mode: true = hands stay visible, background transparent
    bool inverseMode = false;

    // Border gradient mask settings (to hide black borders)
    float borderMaskSize = 0.217f;     // Size of gradient mask at edges (0.0-0.5)
    float borderMaskSoftness = 0.2f;   // Softness of gradient transition
};

struct SegmentationConfig {
    // Which keying mode was active last (Chroma Key legacy vs AI Segmentation) - persisted
    // so switching to AI Segmentation sticks across restarts instead of silently reverting.
    bool keyingModeAI = false;

    // Dark-background brightness compensation for the AI segmentation model's input only
    // (never affects the displayed image). Off by default - see SegmentationEngine.h.
    bool autoGainEnabled = false;
    float autoGainTarget = 219.0f;   // Target mean L (0-255) the auto-gain lifts dark frames toward
    float autoGainMaxGain = 1.0f;    // Cap on the multiplier, so near-black frames don't turn to noise
    float claheClipLimit = 7.0f;     // Local contrast enhancement strength on the model's input
};

struct OverlayConfig {
    float width = 0.879f;          // Overlay width in meters
    float height = 0.652f;         // Overlay height in meters
    float distance = 0.3f;         // Distance from HMD in meters
    float horizontalOffset = -0.04f; // Horizontal offset (negative = left, positive = right)
    float verticalOffset = -0.06f; // Vertical offset (negative = down)
    float opacity = 1.0f;          // Overall opacity
    float handBrightness = 1.0f;   // Hand brightness/dimming (0.0 = black, 1.0 = normal)
    float xRotation = -93.7f;      // X-axis rotation (pitch) in degrees (-180 to 180)
    float yRotation = -18.7f;      // Y-axis rotation (yaw/pivot) in degrees (-180 to 180)

    // Overlay position in seated/cockpit space (meters)
    float posX = -0.004f;          // X position (left/right)
    float posY = -0.299f;          // Y position (up/down)
    float posZ = -0.486001f;       // Z position (forward/back)

    // Overlay rotation (degrees)
    float pitch = 0.1f;            // Pitch rotation (tilt up/down)
    float yaw = 178.7f;            // Yaw rotation (turn left/right)
    float roll = 9.0f;             // Roll rotation (tilt sideways)

};

class Config {
public:
    static Config& getInstance();

    bool load(const std::string& filename);
    bool save(const std::string& filename);
    void loadDefaults();  // Reset to default values

    // Profile management
    std::vector<std::string> listProfiles();
    bool saveProfile(const std::string& profileName);
    bool loadProfile(const std::string& profileName);
    bool deleteProfile(const std::string& profileName);
    std::string getCurrentProfileName() const;

    CameraConfig camera;
    ChromaKeyConfig chromaKey;
    OverlayConfig overlay;
    InputConfig input;
    SegmentationConfig segmentation;

private:
    std::string currentProfileName = "settings";  // Default to settings.ini

    static std::string getProfilesDirectory();
    static std::string sanitizeProfileName(const std::string& name);
    Config() = default;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
};
