# Quick Start Guide

## Installation (First Time Only)

### 1. Install Tools
- Install Visual Studio 2019+ with C++ Desktop Development
- Install CMake: https://cmake.org/download/
- Install vcpkg:
  ```bash
  git clone https://github.com/Microsoft/vcpkg.git C:\vcpkg
  cd C:\vcpkg
  .\bootstrap-vcpkg.bat
  .\vcpkg integrate install
  ```

### 2. Install Dependencies
```bash
cd C:\vcpkg
.\vcpkg install opencv:x64-windows glew:x64-windows
```

### 3. Download OpenVR SDK
- Download: https://github.com/ValveSoftware/openvr/archive/refs/heads/master.zip
- Extract to `MSFSHandOverlay\libs\openvr\`

### 4. Build
```bash
set VCPKG_ROOT=C:\vcpkg
cd MSFSHandOverlay
build.bat
```

## Daily Use

### 1. Hardware Setup
- Mount two webcams overhead, pointing down at your hands
- Place green cloth/mat under your hands
- Ensure good, even lighting

### 2. Start the Overlay
```bash
cd MSFSHandOverlay\build\bin\Release
MSFSHandOverlay.exe
```

### 3. Launch MSFS in VR
- Start SteamVR
- Launch Microsoft Flight Simulator in VR mode
- Your hands should appear in the cockpit!

## Quick Configuration

Edit `config\settings.ini`:

**If cameras are swapped:**
```ini
LeftCameraIndex = 1
RightCameraIndex = 0
```

**If using blue screen:**
```ini
HueCenter = 240
```

**If too much background shows:**
```ini
HueRange = 40
SaturationMin = 0.2
```

**To adjust position:**
```ini
Distance = 0.6        # Further/closer
VerticalOffset = -0.3 # Up/down
Width = 0.6           # Larger/smaller
```

## Common Issues

**Cameras not working:**
- Check camera indices in Device Manager
- Try USB 3.0 ports
- Update camera drivers

**Background not removed:**
- Ensure even green lighting
- Increase HueRange to 40-50
- Lower SaturationMin to 0.2

**Can't see overlay:**
- Start SteamVR BEFORE running the app
- Increase Distance or change VerticalOffset
- Check Opacity is set to 1.0

**Performance issues:**
- Lower resolution: `FrameWidth = 320`, `FrameHeight = 240`
- Reduce FPS: `FPS = 15`

## Tips for Best Results

1. **Camera positioning**: 50-70cm above hands, angled down
2. **Lighting**: Bright, even light on green screen (avoid shadows)
3. **Camera spacing**: 6-8cm apart for natural stereo effect
4. **Green screen**: Use bright green fabric, not too dark
5. **Hand position**: Keep hands in camera view, not too close/far

## File Structure
```
MSFSHandOverlay/
├── build/
│   └── bin/Release/
│       └── MSFSHandOverlay.exe  ← Run this
├── config/
│   └── settings.ini             ← Edit this
├── shaders/                     ← Don't modify
├── src/                         ← Source code
└── libs/                        ← OpenVR SDK
```

Need more help? See [SETUP.md](SETUP.md) for detailed instructions.
