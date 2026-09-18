# MSFS Hand Overlay

A VR overlay application for Microsoft Flight Simulator that displays your real hands in VR using dual overhead webcams with chroma key (green screen) background removal.

## Features

- Dual stereo webcam support for 3D hand visualization
- Real-time chroma key (green screen) background removal
- OpenVR overlay integration (works with any SteamVR headset)
- Optimized for MSFS cockpit use
- Fixed camera positioning (no runtime adjustment needed)

## Requirements

- Windows 10/11
- SteamVR compatible VR headset
- Two USB webcams positioned overhead
- Green screen/chroma key backdrop behind your hands
- Visual Studio 2019 or later (with C++ desktop development)
- CMake 3.15 or later

## Dependencies

- OpenVR SDK (included in libs folder)
- OpenCV 4.x (for camera capture and image processing)
- GLEW (OpenGL Extension Wrangler)
- OpenGL 3.3+

## Building

1. Install dependencies:
   - Download and install OpenCV from https://opencv.org/releases/
   - Install GLEW via vcpkg: `vcpkg install glew:x64-windows`

2. Download OpenVR SDK:
   - Download from https://github.com/ValveSoftware/openvr
   - Extract to `libs/openvr/`

3. Configure and build:
   ```bash
   mkdir build
   cd build
   cmake ..
   cmake --build . --config Release
   ```

4. Run:
   ```bash
   cd bin/Release
   MSFSHandOverlay.exe
   ```

## Configuration

Edit `config/settings.xml` to adjust:
- Camera indices (which USB cameras to use)
- Chroma key color and threshold
- Overlay position and size
- Opacity settings

## Usage

1. Start SteamVR
2. Position your hands under the overhead cameras with green screen backdrop
3. Launch MSFS in VR mode
4. Run MSFSHandOverlay.exe
5. Your hands should appear as an overlay in the cockpit

## Troubleshooting

- **Cameras not detected**: Check camera indices in settings.xml
- **Green screen not working**: Adjust chroma key threshold in settings.xml
- **Overlay not visible**: Ensure SteamVR is running before launching the app
- **Performance issues**: Reduce camera resolution in settings.xml

## License

This project is provided as-is for personal use.
