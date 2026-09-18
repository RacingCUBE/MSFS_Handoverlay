# MSFS Hand Overlay - Quick Start Guide

## 🚀 5-Minute Setup

### Prerequisites
- ✅ Project built successfully (MSFSHandOverlay.exe and MSFSHandOverlay_Layer.dll exist in `build\bin\Release\`)
- ✅ Two USB cameras connected
- ✅ Administrator access for installation
- ✅ Microsoft Flight Simulator installed
- ✅ VR headset set up and working with MSFS

---

## Step 1: Install API Layer (⚡ 1 minute)

Right-click → **Run as administrator**:
```
install_api_layer.bat
```

✅ Success message should appear.

---

## Step 2: Enable Layer (⚡ 1 minute)

1. Download: https://github.com/fredemmott/OpenXR-API-Layers-GUI/releases
2. Run `OpenXR-API-Layers-GUI.exe`
3. Find and **enable** ☑ `XR_APILAYER_MSFS_HandOverlay`
4. Close the GUI

---

## Step 3: Start Main App (⚡ 30 seconds)

```bash
cd build\bin\Release
MSFSHandOverlay.exe
```

**Verify:**
- ✅ Console shows: "Shared memory initialized - API layer ready"
- ✅ Preview window shows stereo camera feed
- ✅ No error messages

---

## Step 4: Launch MSFS (⚡ 2-3 minutes)

1. Start Microsoft Flight Simulator
2. Load any aircraft
3. Enter **VR mode**
4. **Put on your VR headset**
5. **Look down and to your left** (kneeboard area)

👋 **Your hands should appear in VR!**

---

## ✅ Success Checklist

- [ ] API layer installed to `C:\ProgramData\OpenXR\1\api_layers\explicit.d\`
- [ ] Layer enabled in OpenXR API Layers GUI
- [ ] Main app running with preview window
- [ ] Console shows "Shared memory initialized"
- [ ] MSFS running in VR mode
- [ ] Overlay visible at kneeboard position

---

## 🎛️ Adjust Position

Edit `config\settings.ini`:

```ini
[Overlay]
KneeboardPosX = -0.689   # ← → Left/Right
KneeboardPosY = 1.747    # ↑ ↓ Up/Down
KneeboardPosZ = -0.535   # ⬆ ⬇ Forward/Back
KneeboardYaw = 28.8      # 🔄 Rotation
Width = 0.3              # Size
Height = 0.225
Opacity = 0.9
```

Changes apply **instantly** (no restart needed).

---

## 🐛 Not Working?

### No overlay in VR?
1. Check main app is running (console + preview window)
2. Verify layer enabled in OpenXR GUI
3. Restart MSFS after enabling layer

### Overlay at wrong position?
- Adjust `KneeboardPosX/Y/Z` in settings.ini
- Default is kneeboard area (left, low, forward)

### Cameras not detected?
- Check preview window - should show stereo feed
- Verify cameras connected and not in use by other apps

---

## 📁 File Locations

| Item | Location |
|------|----------|
| Main app | `build\bin\Release\MSFSHandOverlay.exe` |
| API layer DLL | `C:\ProgramData\OpenXR\1\api_layers\explicit.d\MSFSHandOverlay_Layer.dll` |
| Manifest | `C:\ProgramData\OpenXR\1\api_layers\explicit.d\MSFSHandOverlay_api_layer.json` |
| Settings | `build\bin\Release\config\settings.ini` |

---

## 🎯 What You Should See

### In Main App Console:
```
[IPC] Shared memory created successfully ✅
Position updated in shared memory:
  Position: (-0.689, 1.747, -0.535)
[VROverlay] Shared memory initialized - API layer ready
```

### In Preview Window:
- Side-by-side stereo camera view
- Green screen removed (transparent)
- Real-time video at ~30 FPS

### In VR (MSFS):
- Your hands floating in cockpit space
- Locked to cockpit (doesn't move with head)
- Stays in position after view resets

---

## 💡 Tips

- **First time**: Position might need adjustment per aircraft
- **Performance**: If laggy, lower camera resolution
- **Visibility**: Adjust opacity in settings.ini (0.0 = invisible, 1.0 = solid)
- **Debugging**: Keep console window visible to see frame updates

---

**That's it! You should now have hand tracking in MSFS VR! 🎉**

For detailed troubleshooting, see **IMPLEMENTATION_COMPLETE.md**
