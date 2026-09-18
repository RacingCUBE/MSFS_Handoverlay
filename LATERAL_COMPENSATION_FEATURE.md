# Lateral (Translation) Movement Compensation Feature

## Overview
Added compensation for **lateral head movement** (translation, not rotation). This compensates for when you physically move your head sideways, up/down, or forward/backward, not just when you rotate it.

## What Was Added

### 1. New Configuration Parameters (Config.h)
Three new compensation strengths in `OverlayConfig`:
```cpp
float lateralCompensationStrength = 0.3f;   // Lateral (sideways) compensation (0.0-1.0)
float verticalTransCompensation = 0.3f;     // Vertical translation compensation (0.0-1.0)
float depthCompensationStrength = 0.0f;     // Depth (forward/back) compensation (0.0-1.0)
```

### 2. Position Tracking (VROverlay.h/.cpp)
Added tracking for initial head position and current compensation:
```cpp
float m_initialHeadPosX, m_initialHeadPosY, m_initialHeadPosZ;  // Initial position
float m_currentCompensationX, Y, Z;  // Current world-space compensation
```

### 3. Compensation Logic (VROverlay.cpp)
In `updateWorldPosition()`:
- Captures initial head position when overlay position is reset
- Tracks current head position every frame
- Calculates position delta (how much head has moved)
- Applies counteractive compensation in world space
- Uses same smoothing as rotational compensation

### 4. GUI Controls (main.cpp)
Added three new sliders in the "Dynamic Parallax Compensation" section:
- **Lateral (Sideways) Strength** (0.0-1.0)
- **Vertical Trans Strength** (0.0-1.0)
- **Depth Strength** (0.0-1.0)

### 5. Config File Support (Config.cpp)
Settings are saved/loaded from `[Overlay]` section in settings.ini:
```ini
LateralCompensationStrength = 0.3   # Lateral (sideways) translation
VerticalTransCompensation = 0.3     # Vertical translation
DepthCompensationStrength = 0.0     # Depth (forward/back)
```

## How It Works

### The Problem:
When you physically move your head sideways (not rotate), the parallax between the cameras and your real hands changes, causing misalignment.

### The Solution:
1. **Capture Initial Position**: When you press reset (button 9 or joystick button), the system records your current head position
2. **Track Movement**: Every frame, the system compares your current head position to the initial position
3. **Project Movement**: World-space position delta is projected onto HMD's orientation axes:
   - **Lateral component** = Movement along HMD's right vector (perpendicular to view)
   - **Vertical component** = Movement along HMD's up vector
   - **Depth component** = Movement along HMD's forward vector (toward/away from view)
4. **Apply Compensation**: The overlay is shifted relative to HMD orientation to follow your head movement:
   - Head moves right (from HMD's perspective) → Overlay shifts right along overlay's right axis
   - Head moves up (from HMD's perspective) → Overlay shifts up along overlay's up axis
   - Head moves forward (from HMD's perspective) → Overlay shifts forward along overlay's forward axis
5. **Smoothing**: Uses the same smoothing factor as rotational compensation for smooth transitions

### Compensation Formula:
```cpp
lateralOffset = positionDelta * compensationStrength
```
- **0.0** = No compensation (overlay stays fixed in world space)
- **0.5** = Half compensation (overlay follows 50% of your movement)
- **1.0** = Full compensation (overlay follows 100% of your movement, stays head-locked)

## How to Use

### Building:
1. **Close the running application** (if open)
2. Run build command:
```bash
cd "C:\Users\T4A-4\source\repos\MSFSHandOverlay_v1\MSFSHandOverlay\build"
cmake --build . --config Release
```

### Configuration:
1. **Enable Dynamic Compensation** (checkbox)
2. **Press Reset** (9 key or joystick button) to capture initial position
3. **Adjust Lateral Compensation Sliders**:
   - **Lateral (Sideways)**: Start at 0.3, increase if hands don't follow sideways movement
   - **Vertical Trans**: Start at 0.3, increase if hands don't follow vertical movement
   - **Depth**: Usually keep at 0.0 unless you need forward/back compensation
4. **Save to Config File** to persist settings

### Recommended Settings:
```ini
# For seated VR with mostly rotational movement:
LateralCompensationStrength = 0.3
VerticalTransCompensation = 0.2
DepthCompensationStrength = 0.0

# For standing VR with more lateral movement:
LateralCompensationStrength = 0.5
VerticalTransCompensation = 0.4
DepthCompensationStrength = 0.2

# For minimal compensation (natural parallax):
LateralCompensationStrength = 0.1
VerticalTransCompensation = 0.1
DepthCompensationStrength = 0.0
```

## Combining with Rotational Compensation

The system now handles **both** types of head movement:

### Rotational Compensation (Already Existed):
- **Horizontal Strength**: Compensates for head yaw (looking left/right)
- **Vertical Strength**: Compensates for head pitch (looking up/down)

### Translation Compensation (NEW):
- **Lateral Strength**: Compensates for head sliding sideways
- **Vertical Trans**: Compensates for head moving up/down
- **Depth Strength**: Compensates for head moving forward/backward

All use the same **Compensation Smoothing** value for consistent feel.

## Troubleshooting

**Hands drift when moving head sideways:**
- Increase "Lateral (Sideways) Strength"
- Make sure "Enable Dynamic Compensation" is checked
- Press reset (9 key) to recalibrate

**Overlay feels too locked to head:**
- Reduce lateral compensation strengths
- Some parallax is natural and helps depth perception

**Jittery/unstable overlay:**
- Increase "Compensation Smoothing" (0.7-0.85)
- Check camera tracking quality

**Hands don't follow vertical head movement:**
- Increase "Vertical Trans Strength"

## Technical Notes

- **Coordinate System**: Uses OpenVR's standing universe coordinate system
- **Compensation Space**: Applied **relative to HMD orientation** (perpendicular to your view)
  - Lateral = Along HMD's right vector (your perceived left/right)
  - Vertical = Along HMD's up vector (your perceived up/down)
  - Depth = Along HMD's forward vector (your perceived forward/back)
- **Projection**: World-space movement is projected onto HMD axes for compensation
- **Smoothing**: Exponential smoothing with configurable factor
- **Independence**: Lateral compensation works independently from rotational compensation
- **Performance**: Negligible impact, runs every frame in `updateWorldPosition()`

## Example Scenarios

### Scenario 1: Leaning to See Instruments
You lean to the right to see a gauge → Without compensation, your virtual hands stay fixed in world space → With lateral compensation (0.5), hands follow your movement halfway, reducing parallax

### Scenario 2: Standing Up/Sitting Down
You stand up from seated position → Without vertical compensation, hands stay at original height → With vertical trans compensation (0.4), hands follow your vertical movement, staying more aligned

### Scenario 3: Leaning Forward
You lean forward to reach a switch → Without depth compensation, hands stay at original distance → With depth compensation (0.3), hands move forward with you, maintaining better alignment

Enjoy more stable hand tracking! 🎮
