# Architecture Overview

## System Design

```
┌─────────────────────────────────────────────────────────────┐
│                    MSFS Hand Overlay                        │
│                   VR Application Flow                        │
└─────────────────────────────────────────────────────────────┘

┌──────────────┐     ┌──────────────┐
│ Left Camera  │     │ Right Camera │
│   (USB 0)    │     │   (USB 1)    │
└──────┬───────┘     └──────┬───────┘
       │                     │
       │   OpenCV Capture    │
       └─────────┬───────────┘
                 │
                 ▼
         ┌───────────────┐
         │ CameraCapture │
         │     Class     │
         └───────┬───────┘
                 │
                 │ cv::Mat frames
                 ▼
         ┌───────────────┐
         │  Chroma Key   │
         │    Shader     │ ◄───── Config (HSV values)
         └───────┬───────┘
                 │
                 │ RGBA with alpha
                 ▼
         ┌───────────────┐
         │ Side-by-Side  │
         │ Stereo Texture│
         │ [Left|Right]  │
         └───────┬───────┘
                 │
                 │ OpenGL Texture
                 ▼
         ┌───────────────┐
         │  VROverlay    │
         │     Class     │
         └───────┬───────┘
                 │
                 │ OpenVR API
                 ▼
         ┌───────────────┐
         │   SteamVR     │
         │   Runtime     │
         └───────┬───────┘
                 │
                 ▼
         ┌───────────────┐
         │   VR Headset  │
         │  (Left Eye)   │   (Right Eye)
         │  [Hands img]  │   [Hands img]
         └───────────────┘
```

## Component Breakdown

### 1. CameraCapture (CameraCapture.h/cpp)
**Purpose**: Capture frames from dual USB webcams

**Key Functions**:
- `initialize()`: Open two cameras using OpenCV with DirectShow backend
- `captureFrames()`: Grab simultaneous frames from both cameras
- `release()`: Clean up camera resources

**Technical Details**:
- Uses `cv::VideoCapture` with `cv::CAP_DSHOW` for Windows
- Configurable resolution and frame rate
- Automatically flips frames horizontally for mirror effect

### 2. ShaderProgram (ShaderProgram.h/cpp)
**Purpose**: Manage OpenGL shaders for chroma key processing

**Key Functions**:
- `loadFromFiles()`: Load and compile GLSL shaders
- `setUniform*()`: Set shader parameters (chroma key color, thresholds)
- `use()/unuse()`: Activate/deactivate shader

**Technical Details**:
- Compiles vertex and fragment shaders
- Links into OpenGL program
- Provides uniform setting helpers

### 3. VROverlay (VROverlay.h/cpp)
**Purpose**: Interface with OpenVR to display overlay in VR

**Key Functions**:
- `initialize()`: Connect to SteamVR and create overlay
- `updateTexture()`: Send OpenGL texture to VR runtime
- `setOverlayTransform()`: Position overlay relative to HMD
- `show()/hide()`: Toggle overlay visibility

**Technical Details**:
- Uses OpenVR Overlay API (not Scene application)
- Configures stereo side-by-side rendering
- Positions overlay relative to HMD (follows head movement)

### 4. Config (Config.h/cpp)
**Purpose**: Load/save application settings

**Key Functions**:
- `load()`: Read INI file
- `save()`: Write INI file
- Singleton pattern for global access

**Settings Categories**:
- **Camera**: Device indices, resolution, FPS
- **ChromaKey**: HSV color space thresholds
- **Overlay**: Size, position, opacity

### 5. Main Application (main.cpp)
**Purpose**: Application entry point and main loop

**Initialization Flow**:
1. Load configuration from INI file
2. Initialize OpenGL (GLEW)
3. Initialize OpenVR overlay
4. Open dual cameras
5. Load chroma key shaders
6. Create stereo texture (side-by-side)
7. Enter main loop

**Main Loop**:
```cpp
while (running) {
    1. Capture frames from both cameras
    2. Render left frame to left half of stereo texture (with chroma key)
    3. Render right frame to right half of stereo texture (with chroma key)
    4. Send stereo texture to VR overlay
    5. Calculate and display FPS
}
```

## Rendering Pipeline

### Step 1: Camera Capture
```
Camera 0 → cv::Mat (BGR, 640x480)
Camera 1 → cv::Mat (BGR, 640x480)
```

### Step 2: OpenGL Texture Upload
```
cv::Mat → glTexImage2D → GL_TEXTURE_2D (temporary)
```

### Step 3: Chroma Key Shader Processing
```glsl
// Fragment shader (chromakey_fragment.glsl)
1. Sample camera texture
2. Convert RGB → HSV color space
3. Calculate hue difference from green (120°)
4. Check saturation and value thresholds
5. Set alpha channel based on match (0 = fully transparent)
6. Output RGBA with transparent background
```

### Step 4: Side-by-Side Composition
```
Framebuffer (1280x480):
┌─────────────┬─────────────┐
│  Left Eye   │  Right Eye  │
│   640x480   │   640x480   │
│ (Camera 0)  │ (Camera 1)  │
└─────────────┴─────────────┘
```

### Step 5: VR Overlay Display
```
OpenVR takes stereo texture and:
- Sends left half to left eye display
- Sends right half to right eye display
- Positions overlay in 3D space relative to HMD
- Handles distortion correction automatically
```

## Key Technologies

### OpenVR API
- **Purpose**: VR runtime integration
- **Mode**: Overlay application (not Scene)
- **Features**: Stereo rendering, HMD-relative positioning
- **Alternative**: Could use OpenXR for broader headset support

### OpenGL 3.3+
- **Purpose**: GPU-accelerated rendering
- **Features**: Texture mapping, framebuffers, shaders
- **Why not Vulkan/DX12**: OpenVR has best OpenGL support, simpler

### OpenCV
- **Purpose**: Camera capture and basic image processing
- **Backend**: DirectShow (Windows native)
- **Why not Media Foundation**: OpenCV provides easier cross-platform API

### GLSL Shaders
- **Purpose**: Real-time chroma key (green screen removal)
- **Advantage**: GPU parallel processing, 60+ FPS easily
- **Color Space**: HSV for better color keying than RGB

## Performance Considerations

### Bottlenecks
1. **Camera capture**: USB bandwidth (use USB 3.0)
2. **Texture upload**: CPU→GPU transfer (minimize by reusing textures)
3. **Shader processing**: Usually fast on modern GPUs

### Optimizations
- Reuse texture objects (don't recreate each frame)
- Use DirectShow backend for lower latency
- Side-by-side rendering (one draw call per eye)
- No intermediate CPU processing (camera → GPU directly)

### Typical Performance
- 640x480 @ 30 FPS: ~3-5ms per frame
- GPU usage: <10%
- CPU usage: <15%
- Latency: ~50-80ms (camera to display)

## Configuration Tuning

### Chroma Key Quality
```ini
HueRange = 30      # Lower = stricter (less background)
SaturationMin = 0.3 # Higher = only vibrant greens removed
EdgeSoftness = 0.05 # Higher = smoother edges (but less sharp)
```

### Stereo 3D Effect
```
Camera spacing: 6-8cm apart → Natural depth
Too close: Flat looking
Too far: Eye strain, excessive depth
```

### Overlay Position
```ini
Distance = 0.6         # Sweet spot for hand interaction
VerticalOffset = -0.2  # Down to see hands in cockpit view
```

## Extending the Application

### Add Features
1. **Controller input**: Use OpenVR input API to adjust settings in VR
2. **Multiple overlays**: Create separate overlays for keyboard, throttle
3. **Edge detection**: Add outline shader for better visibility
4. **Recording**: Capture mixed reality output
5. **Auto-calibration**: Detect green screen automatically

### Alternative Approaches
1. **AI segmentation**: Use ML model instead of chroma key (slower but no green screen needed)
2. **Depth cameras**: Use Intel RealSense for automatic background removal
3. **Pass-through**: Use Quest 3 pass-through API (headset-specific)

## File Dependencies

```
main.cpp
├─ VROverlay.h → openvr.h
├─ CameraCapture.h → opencv2/opencv.hpp
├─ ShaderProgram.h → GL/glew.h
└─ Config.h → (STL only)

CMakeLists.txt links:
- OpenGL
- GLEW
- OpenCV
- OpenVR (openvr_api.lib/dll)
```

## Build Output

```
build/bin/Release/
├── MSFSHandOverlay.exe    # Main executable
├── openvr_api.dll         # OpenVR runtime
├── opencv_world4xx.dll    # OpenCV (from vcpkg)
├── glew32.dll             # GLEW (from vcpkg)
├── shaders/
│   ├── vertex.glsl
│   └── chromakey_fragment.glsl
└── config/
    └── settings.ini
```

## Comparison to Reality Mixer

| Feature | Reality Mixer | MSFS Hand Overlay |
|---------|---------------|-------------------|
| Purpose | General VR passthrough | MSFS cockpit hands only |
| Adjustability | Full 3D box editing | Fixed position (config file) |
| Complexity | High (many features) | Low (single use case) |
| Performance | Medium | High (optimized) |
| Setup | Complex | Simple |
| Controllers | Full bindings | Not needed |
| Multi-box | Yes (8 boxes) | No (single overlay) |
| Config | XML (complex) | INI (simple) |

## Future Improvements

1. **GUI configurator**: Windows app to adjust settings visually
2. **Auto-start**: Launch with SteamVR automatically
3. **Overlay icons**: SteamVR dashboard integration
4. **Multiple presets**: Save different configs for different setups
5. **Network streaming**: Use IP cameras instead of USB
6. **HDR support**: Better dynamic range
7. **Temporal smoothing**: Reduce chroma key flicker
