# Border Gradient Mask Feature

## Overview
Added a gradient mask feature to conceal black borders that may appear at the edges of the camera frames in the VR overlay.

## What Was Changed

### 1. Shader Enhancement (chromakey_fragment.glsl)
- Added two new uniforms:
  - `uBorderMaskSize`: Controls the size of the gradient mask at edges (0.0-0.5)
  - `uBorderMaskSoftness`: Controls the smoothness of the gradient transition
- Added gradient mask calculation that fades the alpha channel near the edges
- The mask creates a smooth transition from transparent at the very edges to fully opaque at the specified distance from the edges

### 2. Configuration System (Config.h / Config.cpp)
- Added `borderMaskSize` (default: 0.12) to ChromaKeyConfig
- Added `borderMaskSoftness` (default: 0.08) to ChromaKeyConfig
- Both values are saved/loaded from the `[BorderMask]` section in settings.ini
- Values persist across application restarts

### 3. Main Application (main.cpp)
- Added shader uniform assignments for border mask in the `renderCameraToTexture` function
- Added ImGui GUI controls for real-time adjustment:
  - "Mask Size" slider (0.0 to 0.5)
  - "Mask Softness" slider (0.0 to 0.2)
  - Both include tooltips explaining their function
- Settings can be adjusted in real-time and saved to config file

## How to Use

### In the Application:
1. Launch MSFSHandOverlay.exe
2. Press 'V' to enable preview mode to see the effect
3. In the GUI, find the "Border Gradient Mask" section (below Inverse Mode toggle)
4. Adjust "Mask Size":
   - 0.0 = No masking (border visible)
   - 0.12 = Default (masks edges smoothly)
   - 0.5 = Maximum (masks half the frame)
5. Adjust "Mask Softness" for smoother/sharper gradient transitions
6. Click "Save to Config File" to persist your settings

### In the Config File (config/settings.ini):
```ini
[BorderMask]
MaskSize = 0.12  # Size of gradient mask at edges (0.0-0.5)
Softness = 0.08  # Softness of gradient transition
```

## Technical Details

### How It Works:
1. For each pixel, the shader calculates the minimum distance to any edge
2. A smooth gradient is applied using the `smoothstep` function
3. The gradient starts at the edge (0.0) and reaches full opacity at `MaskSize + Softness`
4. The gradient mask is multiplied with the chroma key alpha, so both effects combine

### Benefits:
- Conceals black borders from camera frame edges
- Smooth, professional-looking edge transitions
- No hard cutoffs or visible artifacts
- Works in both normal and inverse chroma key modes
- Applies to both left and right eye cameras
- Minimal performance impact

## Troubleshooting

**If borders are still visible:**
- Increase "Mask Size" value
- Enable preview mode (Press 'V') to see the effect clearly

**If too much of the hand is being masked:**
- Decrease "Mask Size" value
- Position your hands further from the camera edges

**If the transition is too harsh:**
- Increase "Mask Softness" value

**If the transition is too blurry:**
- Decrease "Mask Softness" value
