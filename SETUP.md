# Setup Instructions

## Prerequisites

1. **Visual Studio 2019 or later**
   - Install "Desktop development with C++" workload
   - Download from: https://visualstudio.microsoft.com/downloads/

2. **CMake**
   - Download from: https://cmake.org/download/
   - Add to PATH during installation

3. **vcpkg** (for easy dependency management)
   ```bash
   git clone https://github.com/Microsoft/vcpkg.git
   cd vcpkg
   .\bootstrap-vcpkg.bat
   .\vcpkg integrate install
   ```

## Installing Dependencies

### 1. Install OpenCV via vcpkg
```bash
.\vcpkg install opencv:x64-windows
```

### 2. Install GLEW via vcpkg
```bash
.\vcpkg install glew:x64-windows
```

### 3. Download OpenVR SDK

1. Go to https://github.com/ValveSoftware/openvr
2. Click "Code" → "Download ZIP"
3. Extract the ZIP file
4. Copy the contents to `MSFSHandOverlay/libs/openvr/`

The structure should be:
```
MSFSHandOverlay/
├── libs/
│   └── openvr/
│       ├── headers/
│       │   └── openvr.h
│       └── lib/
│           └── win64/
│               ├── openvr_api.lib
│               └── openvr_api.dll
```

## Building the Project

### Option 1: Using CMake GUI

1. Open CMake GUI
2. Set "Where is the source code" to your `MSFSHandOverlay` folder
3. Set "Where to build the binaries" to `MSFSHandOverlay/build`
4. Click "Configure"
5. Select your Visual Studio version
6. Set CMAKE_TOOLCHAIN_FILE to `[vcpkg_root]/scripts/buildsystems/vcpkg.cmake`
7. Click "Generate"
8. Click "Open Project" to open in Visual Studio
9. Build the solution (F7 or Build → Build Solution)

### Option 2: Using Command Line

```bash
cd MSFSHandOverlay
mkdir build
cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=[path_to_vcpkg]/scripts/buildsystems/vcpkg.cmake
cmake --build . --config Release
```

Replace `[path_to_vcpkg]` with the actual path to your vcpkg installation.

## Running the Application

1. Make sure SteamVR is installed and running
2. Navigate to `build/bin/Release/`
3. Run `MSFSHandOverlay.exe`

## Camera Setup

### Physical Setup

1. **Mount two webcams overhead**
   - Position them side-by-side (like eyes)
   - Point them down at your hand area
   - Spacing should be around 6-8 cm apart (similar to eye spacing)
   - Distance from hands: 40-60 cm works well

2. **Green screen backdrop**
   - Place a green cloth/mat under your hands
   - Ensure even lighting (no shadows)
   - The green should be behind your hands when viewed from the cameras

### Software Configuration

1. **Find your camera indices**
   - Plug in both cameras
   - Open Windows Camera app to test which is which
   - Usually the first camera plugged in is index 0, second is index 1

2. **Edit config/settings.ini**
   ```ini
   [Camera]
   LeftCameraIndex = 0    # Left camera
   RightCameraIndex = 1   # Right camera
   ```

3. **Adjust chroma key settings**
   - If using blue screen instead of green, change HueCenter to 240
   - Adjust HueRange if too much/little is being keyed out
   - Increase EdgeSoftness for smoother edges

## Troubleshooting

### Cameras not detected
- Check Device Manager → Cameras
- Try different USB ports (use USB 3.0 if available)
- Update camera drivers

### Green screen not working properly
- Ensure good, even lighting
- Adjust `HueRange` in settings.ini (try 40-50 for more tolerance)
- Lower `SaturationMin` and `ValueMin` (try 0.2)

### Overlay not visible in VR
- Make sure SteamVR is running BEFORE starting the app
- Check that the overlay is not behind you (adjust Distance in settings)
- Try increasing Opacity to 1.0

### Build errors
- Make sure CMAKE_TOOLCHAIN_FILE points to vcpkg
- Verify all dependencies are installed via vcpkg
- Check that OpenVR SDK is in the correct location

### Performance issues
- Reduce camera resolution (try 320x240)
- Lower FPS to 15-20
- Reduce TextureQuality in settings

## Configuration Reference

### Camera Settings
- **LeftCameraIndex/RightCameraIndex**: USB camera device indices (0, 1, 2, etc.)
- **FrameWidth/FrameHeight**: Camera resolution (lower = better performance)
- **FPS**: Frame rate (15-30 recommended)

### Chroma Key Settings
- **HueCenter**: Target color hue (Green=120, Blue=240)
- **HueRange**: Tolerance (20-50, higher = more colors removed)
- **SaturationMin**: Minimum color saturation (0.2-0.5)
- **ValueMin**: Minimum brightness (0.2-0.5)
- **EdgeSoftness**: Edge smoothing (0.01-0.1)

### Overlay Settings
- **Width/Height**: Overlay size in meters
- **Distance**: How far in front of you (meters)
- **VerticalOffset**: Vertical position (negative = down)
- **Opacity**: Transparency (0.0-1.0)
