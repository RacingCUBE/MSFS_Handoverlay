#include "Config.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <windows.h>  // For directory operations

Config& Config::getInstance() {
    static Config instance;
    return instance;
}

bool Config::load(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to open config file: " << filename << std::endl;
        return false;
    }

    std::string line;
    std::string section;

    while (std::getline(file, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }

        // Check for section headers [Section]
        if (line[0] == '[') {
            size_t end = line.find(']');
            if (end != std::string::npos) {
                section = line.substr(1, end - 1);
            }
            continue;
        }

        // Parse key=value pairs
        size_t delimiter = line.find('=');
        if (delimiter == std::string::npos) {
            continue;
        }

        std::string key = line.substr(0, delimiter);
        std::string value = line.substr(delimiter + 1);

        // Trim whitespace
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));

        // Strip inline comments (anything after # or ;)
        size_t commentPos = value.find_first_of("#;");
        if (commentPos != std::string::npos) {
            value = value.substr(0, commentPos);
        }

        // Trim trailing whitespace after removing comment
        value.erase(value.find_last_not_of(" \t") + 1);

        // Parse values based on section
        try {
            if (section == "Camera") {
                if (key == "LeftCameraIndex") camera.leftCameraIndex = std::stoi(value);
                else if (key == "RightCameraIndex") camera.rightCameraIndex = std::stoi(value);
                else if (key == "FrameWidth") camera.frameWidth = std::stoi(value);
                else if (key == "FrameHeight") camera.frameHeight = std::stoi(value);
                else if (key == "FPS") camera.fps = std::stoi(value);
                else if (key == "SplitOffsetPixels") camera.splitOffsetPixels = std::stoi(value);
                else if (key == "UseValveBuiltInCamera") camera.useValveBuiltInCamera = (std::stoi(value) != 0);
                // Separate offsets for left and right cameras
                else if (key == "LeftPixelOffsetX") camera.leftPixelOffsetX = std::stoi(value);
                else if (key == "LeftPixelOffsetY") camera.leftPixelOffsetY = std::stoi(value);
                else if (key == "RightPixelOffsetX") camera.rightPixelOffsetX = std::stoi(value);
                else if (key == "RightPixelOffsetY") camera.rightPixelOffsetY = std::stoi(value);
                else if (key == "LeftBrightness") camera.leftBrightness = std::stof(value);
                else if (key == "LeftContrast") camera.leftContrast = std::stof(value);
                else if (key == "RightBrightness") camera.rightBrightness = std::stof(value);
                else if (key == "RightContrast") camera.rightContrast = std::stof(value);
            }
            else if (section == "ChromaKeyLeft") {
                if (key == "HueCenter") chromaKey.leftHueCenter = std::stof(value);
                else if (key == "HueRange") chromaKey.leftHueRange = std::stof(value);
                else if (key == "SaturationMin") chromaKey.leftSaturationMin = std::stof(value);
                else if (key == "ValueMin") chromaKey.leftValueMin = std::stof(value);
                else if (key == "EdgeSoftness") chromaKey.leftEdgeSoftness = std::stof(value);
                else if (key == "InverseMode") chromaKey.inverseMode = (std::stoi(value) != 0);
            }
            else if (section == "ChromaKeyRight") {
                if (key == "HueCenter") chromaKey.rightHueCenter = std::stof(value);
                else if (key == "HueRange") chromaKey.rightHueRange = std::stof(value);
                else if (key == "SaturationMin") chromaKey.rightSaturationMin = std::stof(value);
                else if (key == "ValueMin") chromaKey.rightValueMin = std::stof(value);
                else if (key == "EdgeSoftness") chromaKey.rightEdgeSoftness = std::stof(value);
            }
            else if (section == "ChromaKeyLeftInverse") {
                if (key == "HueCenter") chromaKey.leftHueCenterInverse = std::stof(value);
                else if (key == "HueRange") chromaKey.leftHueRangeInverse = std::stof(value);
                else if (key == "SaturationMin") chromaKey.leftSaturationMinInverse = std::stof(value);
                else if (key == "ValueMin") chromaKey.leftValueMinInverse = std::stof(value);
                else if (key == "EdgeSoftness") chromaKey.leftEdgeSoftnessInverse = std::stof(value);
            }
            else if (section == "ChromaKeyRightInverse") {
                if (key == "HueCenter") chromaKey.rightHueCenterInverse = std::stof(value);
                else if (key == "HueRange") chromaKey.rightHueRangeInverse = std::stof(value);
                else if (key == "SaturationMin") chromaKey.rightSaturationMinInverse = std::stof(value);
                else if (key == "ValueMin") chromaKey.rightValueMinInverse = std::stof(value);
                else if (key == "EdgeSoftness") chromaKey.rightEdgeSoftnessInverse = std::stof(value);
            }
            else if (section == "BorderMask") {
                if (key == "MaskSize") chromaKey.borderMaskSize = std::stof(value);
                else if (key == "Softness") chromaKey.borderMaskSoftness = std::stof(value);
            }
            else if (section == "Overlay") {
                if (key == "Width") overlay.width = std::stof(value);
                else if (key == "Height") overlay.height = std::stof(value);
                // Overlay position and rotation (accept old "Kneeboard" keys for backward compat)
                else if (key == "PosX" || key == "KneeboardPosX") overlay.posX = std::stof(value);
                else if (key == "PosY" || key == "KneeboardPosY") overlay.posY = std::stof(value);
                else if (key == "PosZ" || key == "KneeboardPosZ") overlay.posZ = std::stof(value);
                else if (key == "Pitch" || key == "KneeboardPitch") overlay.pitch = std::stof(value);
                else if (key == "Yaw" || key == "KneeboardYaw") overlay.yaw = std::stof(value);
                else if (key == "Roll" || key == "KneeboardRoll") overlay.roll = std::stof(value);
                // Legacy head-locked mode
                else if (key == "Distance") overlay.distance = std::stof(value);
                else if (key == "HorizontalOffset") overlay.horizontalOffset = std::stof(value);
                else if (key == "VerticalOffset") overlay.verticalOffset = std::stof(value);
                else if (key == "Opacity") overlay.opacity = std::stof(value);
                else if (key == "XRotation") overlay.xRotation = std::stof(value);
                else if (key == "YRotation") overlay.yRotation = std::stof(value);
                else if (key == "ZRotation") overlay.yRotation = std::stof(value);  // Legacy support
                else if (key == "HandBrightness") overlay.handBrightness = std::stof(value);
            }
            else if (section == "Input") {
                if (key == "JoystickResetButton") input.joystickResetButton = std::stoi(value);
                else if (key == "JoystickID") input.joystickID = std::stoi(value);
            }
            else if (section == "Segmentation") {
                if (key == "KeyingModeAI") segmentation.keyingModeAI = (std::stoi(value) != 0);
                else if (key == "AutoGainEnabled") segmentation.autoGainEnabled = (std::stoi(value) != 0);
                else if (key == "AutoGainTarget") segmentation.autoGainTarget = std::stof(value);
                else if (key == "AutoGainMaxGain") segmentation.autoGainMaxGain = std::stof(value);
                else if (key == "ClaheClipLimit") segmentation.claheClipLimit = std::stof(value);
            }
            else if (section == "Touch") {
                if (key == "Enabled") touch.enabled = (std::stoi(value) != 0);
                else if (key == "JoystickID") touch.joystickID = std::stoi(value);
                else if (key == "EntryEdge") touch.entryEdge = std::stoi(value);
                else if (key == "MatchMaxDistNorm") touch.matchMaxDistNorm = std::stof(value);
                else if (key == "AxisFilterAlpha") touch.axisFilterAlpha = std::stof(value);
                else if (key == "ButtonCount") {
                    int count = std::stoi(value);
                    if (count > 0) touch.buttons.resize(static_cast<size_t>(count));
                }
            }
            else if (section.rfind("TouchButton", 0) == 0) {
                // Section name like "TouchButton3" - the trailing digits index touch.buttons.
                std::string idxStr = section.substr(std::string("TouchButton").length());
                if (!idxStr.empty() && idxStr.find_first_not_of("0123456789") == std::string::npos) {
                    size_t idx = static_cast<size_t>(std::stoul(idxStr));
                    if (idx >= touch.buttons.size()) touch.buttons.resize(idx + 1);
                    if (key == "Name") touch.buttons[idx].name = value;
                    else if (key == "Zone") touch.buttons[idx].zone = std::stoi(value);
                    else if (key == "IsLeftEye") touch.buttons[idx].isLeftEye = (std::stoi(value) != 0);
                    else if (key == "XNorm") touch.buttons[idx].xNorm = std::stof(value);
                    else if (key == "YNorm") touch.buttons[idx].yNorm = std::stof(value);
                    else if (key == "Type") touch.buttons[idx].type = static_cast<TouchControlType>(std::stoi(value));
                }
            }
        }
        catch (const std::exception& e) {
            std::cerr << "Error parsing config value: " << key << " = " << value << std::endl;
        }
    }

    file.close();
    return true;
}

void Config::loadDefaults() {
    // Load defaults from the settings.ini file instead of hardcoded values
    std::string defaultConfigPath = "config/settings.ini";
    if (!load(defaultConfigPath)) {
        std::cerr << "Failed to load defaults from file, using hardcoded defaults" << std::endl;
        // Fallback to hardcoded defaults if file doesn't exist
        camera = CameraConfig();
        chromaKey = ChromaKeyConfig();
        overlay = OverlayConfig();
        input = InputConfig();
        segmentation = SegmentationConfig();
        touch = TouchConfig();
    } else {
        std::cout << "Loaded default settings from " << defaultConfigPath << std::endl;
    }
}

bool Config::save(const std::string& filename) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to save config file: " << filename << std::endl;
        return false;
    }

    file << "# MSFS Hand Overlay Configuration\n\n";

    file << "[Camera]\n";
    file << "LeftCameraIndex = " << camera.leftCameraIndex << "\n";
    file << "RightCameraIndex = " << camera.rightCameraIndex << "\n";
    file << "FrameWidth = " << camera.frameWidth << "\n";
    file << "FrameHeight = " << camera.frameHeight << "\n";
    file << "FPS = " << camera.fps << "\n";
    file << "SplitOffsetPixels = " << camera.splitOffsetPixels << "  # Manual frame-tear split fix, LEFT camera only (0 = off)\n";
    file << "UseValveBuiltInCamera = " << (camera.useValveBuiltInCamera ? 1 : 0) << "  # Capture source: 0 = external USB cameras, 1 = Valve Index built-in\n";
    file << "# Separate pixel offsets for each camera (since they're separate physical cameras)\n";
    file << "LeftPixelOffsetX = " << camera.leftPixelOffsetX << "  # Left camera X offset\n";
    file << "LeftPixelOffsetY = " << camera.leftPixelOffsetY << "  # Left camera Y offset\n";
    file << "RightPixelOffsetX = " << camera.rightPixelOffsetX << "  # Right camera X offset\n";
    file << "RightPixelOffsetY = " << camera.rightPixelOffsetY << "  # Right camera Y offset\n";
    file << "LeftBrightness = " << camera.leftBrightness << "  # Left camera brightness offset (0 = unchanged)\n";
    file << "LeftContrast = " << camera.leftContrast << "  # Left camera contrast gain (1.0 = unchanged)\n";
    file << "RightBrightness = " << camera.rightBrightness << "  # Right camera brightness offset (0 = unchanged)\n";
    file << "RightContrast = " << camera.rightContrast << "  # Right camera contrast gain (1.0 = unchanged)\n\n";

    file << "[ChromaKeyLeft]\n";
    file << "HueCenter = " << chromaKey.leftHueCenter << "  # Green = 120, Blue = 240\n";
    file << "HueRange = " << chromaKey.leftHueRange << "  # Tolerance around hue\n";
    file << "SaturationMin = " << chromaKey.leftSaturationMin << "  # Min saturation to key\n";
    file << "ValueMin = " << chromaKey.leftValueMin << "  # Min brightness to key\n";
    file << "EdgeSoftness = " << chromaKey.leftEdgeSoftness << "  # Edge smoothing\n";
    file << "InverseMode = " << (chromaKey.inverseMode ? 1 : 0) << "  # Inverse mode: 1 = hands visible, background transparent\n\n";

    file << "[ChromaKeyRight]\n";
    file << "HueCenter = " << chromaKey.rightHueCenter << "  # Green = 120, Blue = 240\n";
    file << "HueRange = " << chromaKey.rightHueRange << "  # Tolerance around hue\n";
    file << "SaturationMin = " << chromaKey.rightSaturationMin << "  # Min saturation to key\n";
    file << "ValueMin = " << chromaKey.rightValueMin << "  # Min brightness to key\n";
    file << "EdgeSoftness = " << chromaKey.rightEdgeSoftness << "  # Edge smoothing\n\n";

    file << "[ChromaKeyLeftInverse]\n";
    file << "HueCenter = " << chromaKey.leftHueCenterInverse << "  # Green = 120, Blue = 240\n";
    file << "HueRange = " << chromaKey.leftHueRangeInverse << "  # Tolerance around hue\n";
    file << "SaturationMin = " << chromaKey.leftSaturationMinInverse << "  # Min saturation to key\n";
    file << "ValueMin = " << chromaKey.leftValueMinInverse << "  # Min brightness to key\n";
    file << "EdgeSoftness = " << chromaKey.leftEdgeSoftnessInverse << "  # Edge smoothing\n\n";

    file << "[ChromaKeyRightInverse]\n";
    file << "HueCenter = " << chromaKey.rightHueCenterInverse << "  # Green = 120, Blue = 240\n";
    file << "HueRange = " << chromaKey.rightHueRangeInverse << "  # Tolerance around hue\n";
    file << "SaturationMin = " << chromaKey.rightSaturationMinInverse << "  # Min saturation to key\n";
    file << "ValueMin = " << chromaKey.rightValueMinInverse << "  # Min brightness to key\n";
    file << "EdgeSoftness = " << chromaKey.rightEdgeSoftnessInverse << "  # Edge smoothing\n\n";

    file << "[BorderMask]\n";
    file << "MaskSize = " << chromaKey.borderMaskSize << "  # Size of gradient mask at edges (0.0-0.5)\n";
    file << "Softness = " << chromaKey.borderMaskSoftness << "  # Softness of gradient transition\n\n";

    file << "[Overlay]\n";
    file << "Width = " << overlay.width << "  # Width in meters\n";
    file << "Height = " << overlay.height << "  # Height in meters\n\n";
    file << "# Overlay position in seated space (cockpit-locked)\n";
    file << "PosX = " << overlay.posX << "  # X position (left/right) - negative=left, positive=right\n";
    file << "PosY = " << overlay.posY << "  # Y position (up/down) - negative=down, positive=up\n";
    file << "PosZ = " << overlay.posZ << "  # Z position (forward/back) - negative=back, positive=forward\n\n";
    file << "# Overlay rotation\n";
    file << "Pitch = " << overlay.pitch << "  # Pitch rotation (tilt up/down)\n";
    file << "Yaw = " << overlay.yaw << "  # Yaw rotation (turn left/right)\n";
    file << "Roll = " << overlay.roll << "  # Roll rotation (tilt sideways)\n\n";
    file << "# LEGACY: Old head-locked system\n";
    file << "Distance = " << overlay.distance << "  # Distance from HMD\n";
    file << "HorizontalOffset = " << overlay.horizontalOffset << "  # Horizontal offset\n";
    file << "VerticalOffset = " << overlay.verticalOffset << "  # Vertical offset\n";
    file << "Opacity = " << overlay.opacity << "  # Overall opacity\n";
    file << "XRotation = " << overlay.xRotation << "  # X-axis rotation (pitch) in degrees\n";
    file << "YRotation = " << overlay.yRotation << "  # Y-axis rotation (yaw/pivot) in degrees\n";
    file << "HandBrightness = " << overlay.handBrightness << "  # Hand brightness for night flying (0.0 = dark, 1.0 = normal)\n\n";

    file << "[Input]\n";
    file << "JoystickResetButton = " << input.joystickResetButton << "  # Joystick button for reset overlay position\n";
    file << "JoystickID = " << input.joystickID << "  # Joystick ID (-1 = auto-detect first joystick)\n\n";

    file << "[Segmentation]\n";
    file << "KeyingModeAI = " << (segmentation.keyingModeAI ? 1 : 0) << "  # Keying mode: 0 = Chroma Key (legacy), 1 = AI Segmentation\n";
    file << "AutoGainEnabled = " << (segmentation.autoGainEnabled ? 1 : 0) << "  # Lift dark frames toward AutoGainTarget before AI segmentation (0/1)\n";
    file << "AutoGainTarget = " << segmentation.autoGainTarget << "  # Target mean brightness (0-255) the auto-gain lifts dark frames toward\n";
    file << "AutoGainMaxGain = " << segmentation.autoGainMaxGain << "  # Cap on the brightness multiplier\n";
    file << "ClaheClipLimit = " << segmentation.claheClipLimit << "  # Local contrast enhancement strength on the AI segmentation model's input\n\n";

    file << "[Touch]\n";
    file << "Enabled = " << (touch.enabled ? 1 : 0) << "  # Enable capacitive-touch fingertip matching\n";
    file << "JoystickID = " << touch.joystickID << "  # GLFW joystick ID of the touch HID gamepad (-1 = auto-detect)\n";
    file << "EntryEdge = " << touch.entryEdge << "  # Which frame edge the arm enters from: 0=Bottom,1=Top,2=Left,3=Right\n";
    file << "MatchMaxDistNorm = " << touch.matchMaxDistNorm << "  # Max normalized distance to accept a fingertip->button match\n";
    file << "AxisFilterAlpha = " << touch.axisFilterAlpha << "  # Dial-rotation angle smoothing: 1.0 = none, smaller = smoother/more lag\n";
    file << "ButtonCount = " << touch.buttons.size() << "\n\n";
    for (size_t i = 0; i < touch.buttons.size(); ++i) {
        const auto& b = touch.buttons[i];
        file << "[TouchButton" << i << "]\n";
        file << "Name = " << b.name << "\n";
        file << "Zone = " << b.zone << "\n";
        file << "IsLeftEye = " << (b.isLeftEye ? 1 : 0) << "\n";
        file << "XNorm = " << b.xNorm << "\n";
        file << "YNorm = " << b.yNorm << "\n";
        file << "Type = " << static_cast<int>(b.type) << "  # 0 = Button, 1 = Dial (rotation center)\n\n";
    }

    file.close();
    return true;
}

// ============== Profile Management Methods ==============

std::string Config::getProfilesDirectory() {
    return "config/profiles/";
}

std::string Config::sanitizeProfileName(const std::string& name) {
    std::string sanitized = name;

    // Remove invalid filename characters
    const std::string invalidChars = "\\/:*?\"<>|";
    for (char& c : sanitized) {
        if (invalidChars.find(c) != std::string::npos) {
            c = '_';
        }
        // Replace spaces with underscores
        if (c == ' ') {
            c = '_';
        }
    }

    // Ensure .ini extension
    if (sanitized.length() < 4 || sanitized.substr(sanitized.length() - 4) != ".ini") {
        sanitized += ".ini";
    }

    return sanitized;
}

std::vector<std::string> Config::listProfiles() {
    std::vector<std::string> profiles;
    std::string profilesDir = getProfilesDirectory();
    std::string searchPath = profilesDir + "*.ini";

    // Use Windows API to list directory contents
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);

    if (hFind == INVALID_HANDLE_VALUE) {
        // Directory doesn't exist or is empty
        return profiles;
    }

    do {
        // Skip directories
        if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            std::string filename = findData.cFileName;
            // Remove .ini extension for display
            if (filename.length() > 4 && filename.substr(filename.length() - 4) == ".ini") {
                filename = filename.substr(0, filename.length() - 4);
            }
            profiles.push_back(filename);
        }
    } while (FindNextFileA(hFind, &findData) != 0);

    FindClose(hFind);

    // Sort alphabetically
    std::sort(profiles.begin(), profiles.end());

    return profiles;
}

bool Config::saveProfile(const std::string& profileName) {
    if (profileName.empty()) {
        std::cerr << "Profile name cannot be empty" << std::endl;
        return false;
    }

    // Create profiles directory if it doesn't exist
    std::string profilesDir = getProfilesDirectory();
    CreateDirectoryA(profilesDir.c_str(), NULL);

    // Sanitize profile name
    std::string sanitizedName = sanitizeProfileName(profileName);
    std::string profilePath = profilesDir + sanitizedName;

    // Save using existing save method
    if (save(profilePath)) {
        currentProfileName = profileName;
        std::cout << "Profile saved: " << profilePath << std::endl;
        return true;
    }

    return false;
}

bool Config::loadProfile(const std::string& profileName) {
    if (profileName.empty()) {
        std::cerr << "Profile name cannot be empty" << std::endl;
        return false;
    }

    // Sanitize profile name
    std::string sanitizedName = sanitizeProfileName(profileName);
    std::string profilePath = getProfilesDirectory() + sanitizedName;

    // Reset all config structs to defaults before loading,
    // so any keys missing from the profile file get clean defaults
    // instead of stale values from the previously loaded profile.
    camera = CameraConfig();
    chromaKey = ChromaKeyConfig();
    overlay = OverlayConfig();
    input = InputConfig();
    touch = TouchConfig();

    // Load using existing load method
    if (load(profilePath)) {
        currentProfileName = profileName;
        std::cout << "Profile loaded: " << profilePath << std::endl;
        return true;
    }

    std::cerr << "Failed to load profile: " << profilePath << std::endl;
    return false;
}

bool Config::deleteProfile(const std::string& profileName) {
    if (profileName.empty()) {
        std::cerr << "Profile name cannot be empty" << std::endl;
        return false;
    }

    // Don't allow deleting the currently loaded profile
    if (profileName == currentProfileName) {
        std::cerr << "Cannot delete currently loaded profile" << std::endl;
        return false;
    }

    // Sanitize profile name
    std::string sanitizedName = sanitizeProfileName(profileName);
    std::string profilePath = getProfilesDirectory() + sanitizedName;

    // Delete the file
    if (DeleteFileA(profilePath.c_str())) {
        std::cout << "Profile deleted: " << profilePath << std::endl;
        return true;
    }

    std::cerr << "Failed to delete profile: " << profilePath << std::endl;
    return false;
}

std::string Config::getCurrentProfileName() const {
    return currentProfileName;
}
