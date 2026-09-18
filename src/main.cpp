#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <atomic>
#include <memory>
#include <algorithm>
#include <type_traits>
#include <cstring>
#include <cmath>
#include <limits>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <opencv2/opencv.hpp>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "VROverlay.h"
#include "CameraCapture.h"
#include "ShaderProgram.h"
#include "Config.h"
#include "FrameData.h"
#include "TripleBuffer.h"
#include "PerformanceStats.h"
#include "D3D11Context.h"
#include "SegmentationEngine.h"
#include "TouchInput.h"
#include "HandTouchTracker.h"

// DirectX 11 interop
#include <d3d11.h>
#include <wrl/client.h>
#define NOMINMAX  // Prevent Windows.h min/max macros from conflicting with std::min/std::max
#include <windows.h>  // For CreateDirectoryA
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

using Microsoft::WRL::ComPtr;

// OpenGL objects
GLuint g_stereoTexture = 0;
GLuint g_framebuffer = 0;
GLuint g_vao = 0;
GLuint g_vbo = 0;
GLuint g_leftCameraTexture = 0;   // Persistent texture for left camera
GLuint g_rightCameraTexture = 0;  // Persistent texture for right camera
GLFWwindow* g_window = nullptr;

// Pixel Buffer Objects for async texture upload (performance optimization)
GLuint g_leftPBO = 0;
GLuint g_rightPBO = 0;

// Pixel Buffer Objects for async framebuffer readback (double-buffered)
GLuint g_readbackPBOs[2] = {0, 0};  // Double-buffer for async readback
int g_currentPBOIndex = 0;
int g_nextPBOIndex = 1;
std::vector<uint8_t> g_pboPixelBuffer;  // Reusable buffer

// Application objects
VROverlay g_vrOverlay;
CameraCapture g_cameras;
ShaderProgram g_chromaKeyShader;
ShaderProgram g_previewShader;

// Tracks which capture source is currently active (false = USB cameras via
// OpenCV, true = Valve Index built-in stereo camera via OpenVR).
static bool g_useValveBuiltInCamera = false;

// Chroma Key (legacy, HSV color-based) vs AI Segmentation (ML-based alpha matting,
// no green screen dependency, avoids color-key spill/fringing artifacts).
enum class KeyingMode { ChromaKey, AISegmentation };
static KeyingMode g_keyingMode = KeyingMode::ChromaKey;
SegmentationEngine g_segmentationEngine;

// Capacitive-touch input: a physical touch on the panel confirms *that* a virtual button
// was pressed but not *which* one. Combined with the fingertip position extracted from the
// AI segmentation mask at the moment of contact (HandTouchTracker.h), this resolves which
// calibrated button was actually touched - see TOUCH_CALIBRATION.md for the full design and
// the hardware/firmware side (arduino/CapacitiveTouchZones/).
TouchInput g_touchInput;
TouchMatchResult g_lastTouchMatch;
std::chrono::steady_clock::time_point g_lastTouchMatchTime;
// When armed, the next touch event's detected fingertip position is written into
// config.touch.buttons[g_touchCalibrationTargetIndex] instead of being matched - this is
// the "physically touch the real button while armed" calibration workflow (Touch
// Calibration tab). g_touchCalibrationTargetEye selects which eye's mask/position to use.
bool g_touchCalibrationArmed = false;
int g_touchCalibrationTargetIndex = -1;
bool g_touchCalibrationTargetEye = true;  // true = left eye

// Tracks an in-progress dial rotation: set when a touch's matched calibration entry is a
// Dial and the touch is still held, cleared when that zone releases. Rotation is measured
// by the hand contour's own twist (computeHandOrientationAngle), not by the fingertip's
// position - see HandTouchTracker.h for why a twisting wrist is the more reliable signal
// than tracking a fingertip sweeping around a pivot.
struct ActiveDialDrag {
    bool active = false;
    int buttonIndex = -1;
    int zone = -1;
    bool isLeftEye = true;
    float lastOrientationRad = 0.0f;
    float totalRotationRad = 0.0f;  // accumulated since this drag started - diagnostic only
    // Simple detent-style counter: +1/-1 per frame the filtered angle moves past the noise
    // threshold in that direction, rather than trying to report a precise degree amount -
    // far more forgiving of a noisy or low-sensitivity signal (see TOUCH_CALIBRATION.md's
    // notes on the axis angle's real-world sensitivity limits) since it only needs the
    // *sign* of the change to be reliable, not its magnitude. This is also naturally close
    // to how SimConnect knob controls actually work (discrete increment/decrement events).
    int tickCount = 0;
    AxisAngleFilter filter;         // smooths the raw per-frame orientation angle
};
ActiveDialDrag g_activeDialDrag;

// Separate filter instances for the live diagnostics readout (Touch Calibration tab) -
// independent of any drag in progress, so the "axis" number can be watched smoothed even
// outside an active touch.
AxisAngleFilter g_axisFilterLeftDiag;
AxisAngleFilter g_axisFilterRightDiag;

// Live, continuously-updated (every frame, independent of any touch event) hand readouts
// for the Touch Calibration tab's diagnostics section - lets fingertip position and hand
// orientation/direction be watched in real time while tuning, without needing a touch
// event to trigger them.
struct LiveHandDiagnostics {
    cv::Point2f fingertipLeft{-1.0f, -1.0f};
    cv::Point2f fingertipRight{-1.0f, -1.0f};
    float orientationLeftDeg = std::numeric_limits<float>::quiet_NaN();   // axis angle, 0-180
    float orientationRightDeg = std::numeric_limits<float>::quiet_NaN();
    float directionLeftDeg = std::numeric_limits<float>::quiet_NaN();     // +-180, see computeHandDirectionAngle
    float directionRightDeg = std::numeric_limits<float>::quiet_NaN();
};
LiveHandDiagnostics g_liveHandDiag;

// True when FlightSimulator.exe is the actual Windows foreground window right now - used to
// pick GPU vs CPU for AI Segmentation per-frame (GPU gives the best matte quality but directly
// competes with MSFS's own rendering for the same GPU, causing severe FPS drops specifically
// while MSFS is focused/actively rendering VR; CPU has no such contention but lower quality -
// switching based on real MSFS focus gets the best of both). Deliberately checks the actual
// Windows foreground window's process name, not this app's own window focus state (those are
// different things - this app's window can be unfocused while some other non-MSFS window,
// not MSFS, is what's focused).
bool isFlightSimulatorForeground() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (pid == 0) return false;
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProcess) return false;
    wchar_t path[MAX_PATH];
    DWORD size = MAX_PATH;
    bool isFlightSim = false;
    if (QueryFullProcessImageNameW(hProcess, 0, path, &size)) {
        std::wstring pathStr(path);
        isFlightSim = pathStr.find(L"FlightSimulator.exe") != std::wstring::npos;
    }
    CloseHandle(hProcess);
    return isFlightSim;
}

// Performance stats
PerformanceStats g_perfStats;

// TESTING ONLY: toggles the CPU chroma-key + on-screen preview render path. Off by default
// because applyCPUChromaKey() is an unoptimized per-pixel loop that's heavy enough to starve
// the main thread's message pump once both cameras are actually streaming (this cost existed
// before the preview window was added - it just never had anything on-screen consuming it).
static bool g_livePreviewEnabled = false;

// Pin-point calibration state
static bool g_showCrosshair = false;  // Show crosshair on overlay when positioning pin
static bool g_pinActive = false;
static float g_pinU = 0.5f, g_pinV = 0.5f;  // Pin position on overlay surface (0-1)
static float g_pinWorldX = 0, g_pinWorldY = 0, g_pinWorldZ = 0;  // Pinned world position

// Rotate a point by overlay rotation (YXZ Euler order, matches API layer quaternion)
static void rotateByOverlay(float pitch, float yaw, float roll,
                            float inX, float inY, float inZ,
                            float& outX, float& outY, float& outZ) {
    float pr = pitch * 3.14159265f / 180.0f;
    float yr = yaw * 3.14159265f / 180.0f;
    float rr = roll * 3.14159265f / 180.0f;
    float cy = cosf(yr * 0.5f), sy = sinf(yr * 0.5f);
    float cp = cosf(pr * 0.5f), sp = sinf(pr * 0.5f);
    float cr = cosf(rr * 0.5f), sr = sinf(rr * 0.5f);
    float qx = sr*cp*cy - cr*sp*sy;
    float qy = cr*sp*cy + sr*cp*sy;
    float qz = cr*cp*sy - sr*sp*cy;
    float qw = cr*cp*cy + sr*sp*sy;
    // Rotate vector by quaternion: v' = v + 2w(q x v) + 2(q x (q x v))
    float cx1 = qy * inZ - qz * inY;
    float cy1 = qz * inX - qx * inZ;
    float cz1 = qx * inY - qy * inX;
    float cx2 = qy * cz1 - qz * cy1;
    float cy2 = qz * cx1 - qx * cz1;
    float cz2 = qx * cy1 - qy * cx1;
    outX = inX + 2.0f * (qw * cx1 + cx2);
    outY = inY + 2.0f * (qw * cy1 + cy2);
    outZ = inZ + 2.0f * (qw * cz1 + cz2);
}

// Set pin: capture the world position of the pin point on the overlay
static void setPin(const OverlayConfig& ov) {
    float pinLocalX = (g_pinU - 0.5f) * ov.width;
    float pinLocalY = (0.5f - g_pinV) * ov.height;
    float rotX, rotY, rotZ;
    rotateByOverlay(ov.pitch, ov.yaw, ov.roll,
                    pinLocalX, pinLocalY, 0.0f, rotX, rotY, rotZ);
    g_pinWorldX = ov.posX + rotX;
    g_pinWorldY = ov.posY + rotY;
    g_pinWorldZ = ov.posZ + rotZ;
    g_pinActive = true;
}

// Apply pin compensation: auto-adjust posX/posY to keep pinned point at its world position
static void applyPinCompensation(OverlayConfig& ov) {
    if (!g_pinActive) return;
    float pinLocalX = (g_pinU - 0.5f) * ov.width;
    float pinLocalY = (0.5f - g_pinV) * ov.height;
    float rotX, rotY, rotZ;
    rotateByOverlay(ov.pitch, ov.yaw, ov.roll,
                    pinLocalX, pinLocalY, 0.0f, rotX, rotY, rotZ);
    ov.posX = g_pinWorldX - rotX;
    ov.posY = g_pinWorldY - rotY;
    // posZ left unchanged - user controls depth freely
}

// Tracking thread coordination
std::atomic<bool> g_trackingRunning{false};
std::unique_ptr<std::thread> g_trackingThread;

// DirectX 11 for VR overlay (hybrid mode)
D3D11Context g_d3dContext;
ComPtr<ID3D11Texture2D> g_d3dStereoTexture;
ComPtr<ID3D11Texture2D> g_d3dSharedTexture;  // Shared with OpenGL
HANDLE g_sharedTextureHandle = nullptr;

// WGL_NV_DX_interop for zero-copy GPU-to-GPU transfer (eliminates CPU roundtrip)
HANDLE g_wglInteropDevice = nullptr;
HANDLE g_wglInteropTexture = nullptr;

// WGL_NV_DX_interop function pointers (manually loaded)
typedef HANDLE (WINAPI * PFNWGLDXOPENDEVICENVPROC) (void *dxDevice);
typedef BOOL (WINAPI * PFNWGLDXCLOSEDEVICENVPROC) (HANDLE hDevice);
typedef HANDLE (WINAPI * PFNWGLDXREGISTEROBJECTNVPROC) (HANDLE hDevice, void *dxObject, GLuint name, GLenum type, GLenum access);
typedef BOOL (WINAPI * PFNWGLDXUNREGISTEROBJECTNVPROC) (HANDLE hDevice, HANDLE hObject);
typedef BOOL (WINAPI * PFNWGLDXLOCKOBJECTSNVPROC) (HANDLE hDevice, GLint count, HANDLE *hObjects);
typedef BOOL (WINAPI * PFNWGLDXUNLOCKOBJECTSNVPROC) (HANDLE hDevice, GLint count, HANDLE *hObjects);

PFNWGLDXOPENDEVICENVPROC wglDXOpenDeviceNV = nullptr;
PFNWGLDXCLOSEDEVICENVPROC wglDXCloseDeviceNV = nullptr;
PFNWGLDXREGISTEROBJECTNVPROC wglDXRegisterObjectNV = nullptr;
PFNWGLDXUNREGISTEROBJECTNVPROC wglDXUnregisterObjectNV = nullptr;
PFNWGLDXLOCKOBJECTSNVPROC wglDXLockObjectsNV = nullptr;
PFNWGLDXUNLOCKOBJECTSNVPROC wglDXUnlockObjectsNV = nullptr;

#define WGL_ACCESS_READ_ONLY_NV 0x0000
#define WGL_ACCESS_READ_WRITE_NV 0x0001
#define WGL_ACCESS_WRITE_DISCARD_NV 0x0002

bool initializeOpenGL() {
    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return false;
    }

    // Create a small visible window for OpenGL context and keyboard input
    // Use compatibility profile for better driver support
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_ANY_PROFILE);

    // Performance: Don't steal focus, but also don't force always-on-top (reduces compositor load)
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);  // Don't steal focus when showing
    // Note: GLFW_FLOATING removed to reduce impact on MSFS performance

    g_window = glfwCreateWindow(800, 600, "MSFSHandOverlay", nullptr, nullptr);
    if (!g_window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(g_window);

    // Disable VSync - use manual frame limiting instead (reduces GPU wait time)
    glfwSwapInterval(0);  // 0 = VSync off (manual 20 FPS cap handles timing)
    std::cout << "VSync disabled - using manual 20 FPS cap for minimal GPU impact" << std::endl;

    // Initialize GLEW
    glewExperimental = GL_TRUE;
    GLenum glewError = glewInit();
    if (glewError != GLEW_OK) {
        std::cerr << "Failed to initialize GLEW: " << glewGetErrorString(glewError) << std::endl;
        glfwTerminate();
        return false;
    }

    std::cout << "OpenGL Version: " << glGetString(GL_VERSION) << std::endl;
    std::cout << "GLEW Version: " << glewGetString(GLEW_VERSION) << std::endl;
    std::cout << "OpenGL Renderer: " << glGetString(GL_RENDERER) << std::endl;

    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Make keyboard navigation more responsive for sliders
    // This allows arrow keys to work immediately after tabbing to a slider
    io.ConfigNavMoveSetMousePos = false;  // Don't move mouse during keyboard nav

    // Setup ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(g_window, true);
    ImGui_ImplOpenGL3_Init("#version 150");

    std::cout << "ImGui initialized successfully" << std::endl;

    // Enable alpha blending
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    std::cout << "OpenGL initialized successfully" << std::endl;
    return true;
}

bool initializeDirectX() {
    // Get native window handle from GLFW
    HWND hwnd = glfwGetWin32Window(g_window);
    if (!hwnd) {
        std::cerr << "Failed to get Win32 window handle" << std::endl;
        return false;
    }

    // Initialize DirectX 11 context
    if (!g_d3dContext.initialize(hwnd, 800, 600)) {
        std::cerr << "Failed to initialize DirectX 11" << std::endl;
        return false;
    }

    // Create DirectX texture for VR overlay (side-by-side stereo)
    Config& config = Config::getInstance();
    int stereoWidth = config.camera.frameWidth * 2;
    int stereoHeight = config.camera.frameHeight;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = stereoWidth;
    desc.Height = stereoHeight;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;

    HRESULT hr = g_d3dContext.getDevice()->CreateTexture2D(&desc, nullptr, g_d3dStereoTexture.GetAddressOf());
    if (FAILED(hr)) {
        std::cerr << "Failed to create DirectX stereo texture" << std::endl;
        return false;
    }

    std::cout << "DirectX 11 initialized successfully (Hybrid Mode)" << std::endl;
    std::cout << "  OpenGL: Camera frame processing" << std::endl;
    std::cout << "  DirectX: VR overlay submission (30-40% faster)" << std::endl;
    return true;
}

/**
 * @brief Initialize WGL_NV_DX_interop for zero-copy GPU-to-GPU transfer
 *
 * This ELIMINATES the CPU roundtrip (glReadPixels + UpdateSubresource) that causes
 * 4-second stalls when the GPU is at 100% load (MSFS running).
 *
 * OpenGL renders directly into a DirectX texture - data never leaves the GPU.
 */
bool initializeWGLInterop() {
    // Load WGL_NV_DX_interop function pointers
    wglDXOpenDeviceNV = (PFNWGLDXOPENDEVICENVPROC)wglGetProcAddress("wglDXOpenDeviceNV");
    wglDXCloseDeviceNV = (PFNWGLDXCLOSEDEVICENVPROC)wglGetProcAddress("wglDXCloseDeviceNV");
    wglDXRegisterObjectNV = (PFNWGLDXREGISTEROBJECTNVPROC)wglGetProcAddress("wglDXRegisterObjectNV");
    wglDXUnregisterObjectNV = (PFNWGLDXUNREGISTEROBJECTNVPROC)wglGetProcAddress("wglDXUnregisterObjectNV");
    wglDXLockObjectsNV = (PFNWGLDXLOCKOBJECTSNVPROC)wglGetProcAddress("wglDXLockObjectsNV");
    wglDXUnlockObjectsNV = (PFNWGLDXUNLOCKOBJECTSNVPROC)wglGetProcAddress("wglDXUnlockObjectsNV");

    // Check if all functions loaded successfully
    if (!wglDXOpenDeviceNV || !wglDXCloseDeviceNV || !wglDXRegisterObjectNV ||
        !wglDXUnregisterObjectNV || !wglDXLockObjectsNV || !wglDXUnlockObjectsNV) {
        std::cerr << "[WGLInterop] ERROR: WGL_NV_DX_interop not supported!" << std::endl;
        std::cerr << "[WGLInterop] Falling back to CPU roundtrip (will cause stalls)" << std::endl;
        return false;
    }

    // 1. Open the DirectX device for interop
    g_wglInteropDevice = wglDXOpenDeviceNV(g_d3dContext.getDevice());
    if (!g_wglInteropDevice) {
        std::cerr << "[WGLInterop] Failed to open DirectX device" << std::endl;
        return false;
    }

    // 2. Register the DirectX texture with OpenGL
    //    OpenGL will render to g_stereoTexture, which is DIRECTLY BACKED by g_d3dStereoTexture
    g_wglInteropTexture = wglDXRegisterObjectNV(
        g_wglInteropDevice,
        g_d3dStereoTexture.Get(),  // DirectX texture (destination)
        g_stereoTexture,             // OpenGL texture (source)
        GL_TEXTURE_2D,
        WGL_ACCESS_WRITE_DISCARD_NV  // OpenGL writes, DirectX reads
    );

    if (!g_wglInteropTexture) {
        std::cerr << "[WGLInterop] Failed to register texture" << std::endl;
        wglDXCloseDeviceNV(g_wglInteropDevice);
        g_wglInteropDevice = nullptr;
        return false;
    }

    std::cout << "[WGLInterop] Initialized successfully - ZERO-COPY GPU transfer enabled!" << std::endl;
    std::cout << "[WGLInterop] CPU roundtrip eliminated, no more 4-second stalls" << std::endl;
    return true;
}

/**
 * @brief Transfer OpenGL texture to DirectX using WGL interop (ZERO-COPY)
 *
 * OLD METHOD (REMOVED): glReadPixels + UpdateSubresource = CPU roundtrip
 *   - GPU → CPU: glReadPixels (stalls for 4+ seconds when GPU at 100%)
 *   - CPU → GPU: UpdateSubresource
 *   - Causes frame bursting when MSFS running
 *
 * NEW METHOD: WGL_NV_DX_interop = Direct GPU-to-GPU
 *   - Lock texture → OpenGL renders → Unlock texture
 *   - Data never leaves GPU, no CPU involvement
 *   - No stalls, smooth 30 FPS even with MSFS at 100% GPU load
 */
void copyOpenGLToDirectX() {
    // If WGL interop is available, use zero-copy path
    if (g_wglInteropDevice && g_wglInteropTexture) {
        // ZERO-COPY: Just lock/unlock - texture is already shared!
        // The framebuffer rendering (processAndRenderFrames) already wrote to g_stereoTexture
        // which is directly backed by g_d3dStereoTexture via WGL interop.
        // We just need to unlock so DirectX can read it.

        // Note: Lock/unlock happens during render, not here.
        // This function is now a no-op when interop is active.
        return;
    }

    // Fallback: Old CPU roundtrip method (will cause stalls)
    // Safety checks
    if (!g_d3dContext.getContext() || !g_d3dStereoTexture.Get()) {
        std::cerr << "[copyOpenGLToDirectX] DirectX not initialized!" << std::endl;
        return;
    }

    if (g_framebuffer == 0) {
        std::cerr << "[copyOpenGLToDirectX] OpenGL framebuffer not created!" << std::endl;
        return;
    }

    Config& config = Config::getInstance();
    int stereoWidth = config.camera.frameWidth * 2;
    int stereoHeight = config.camera.frameHeight;
    size_t pixelDataSize = stereoWidth * stereoHeight * 4;  // RGBA

    // === STEP 1: Start async read from framebuffer to "next" PBO ===
    // This triggers a DMA transfer (GPU→PBO) that happens in the background
    glBindFramebuffer(GL_FRAMEBUFFER, g_framebuffer);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, g_readbackPBOs[g_nextPBOIndex]);
    glReadPixels(0, 0, stereoWidth, stereoHeight, GL_RGBA, GL_UNSIGNED_BYTE, 0);  // 0 = read to PBO
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // === STEP 2: Map "current" PBO and copy to CPU memory ===
    // By the time we get here, the previous frame's DMA should be complete
    glBindBuffer(GL_PIXEL_PACK_BUFFER, g_readbackPBOs[g_currentPBOIndex]);

    // Map with GL_MAP_READ_BIT - this WILL stall if previous frame's DMA not complete
    GLubyte* pboData = (GLubyte*)glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
    if (pboData) {
        // === STEP 3: Upload directly to DirectX texture ===
        // NO CPU-SIDE FLIPPING: Vertex shader handles orientation via uFlipVertical
        // NO MEMCPY: Direct upload from PBO memory (much faster!)
        g_d3dContext.getContext()->UpdateSubresource(
            g_d3dStereoTexture.Get(),
            0,
            nullptr,
            pboData,            // Direct pointer to GPU-readback data
            stereoWidth * 4,
            0
        );

        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    } else {
        std::cerr << "[copyOpenGLToDirectX] Failed to map PBO!" << std::endl;
    }

    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    // === STEP 5: Swap PBO indices for next frame ===
    std::swap(g_currentPBOIndex, g_nextPBOIndex);
}

bool createStereoTexture(int width, int height) {
    // Create a side-by-side stereo texture (double width)
    glGenTextures(1, &g_stereoTexture);
    glBindTexture(GL_TEXTURE_2D, g_stereoTexture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Allocate texture memory (side-by-side, so width * 2)
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width * 2, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glBindTexture(GL_TEXTURE_2D, 0);

    // Create persistent camera textures (reused every frame)
    // RGBA format for CPU-processed chroma key with alpha channel
    glGenTextures(1, &g_leftCameraTexture);
    glBindTexture(GL_TEXTURE_2D, g_leftCameraTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenTextures(1, &g_rightCameraTexture);
    glBindTexture(GL_TEXTURE_2D, g_rightCameraTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glBindTexture(GL_TEXTURE_2D, 0);

    // === PERFORMANCE OPTIMIZATION: Create Pixel Buffer Objects (PBOs) ===
    // PBOs allow async texture upload, reducing CPU-GPU transfer bottleneck
    size_t frameSize = width * height * 4;  // RGBA format, 4 bytes per pixel (CPU chroma key)

    glGenBuffers(1, &g_leftPBO);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, g_leftPBO);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, frameSize, nullptr, GL_STREAM_DRAW);

    glGenBuffers(1, &g_rightPBO);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, g_rightPBO);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, frameSize, nullptr, GL_STREAM_DRAW);

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    std::cout << "Created PBOs for async texture upload (frame size: " << frameSize << " bytes)" << std::endl;

    // === ASYNC READBACK PBOs: Double-buffered for non-blocking glReadPixels ===
    size_t stereoFrameSize = (width * 2) * height * 4;  // RGBA, side-by-side stereo
    g_pboPixelBuffer.resize(stereoFrameSize);

    glGenBuffers(2, g_readbackPBOs);
    for (int i = 0; i < 2; i++) {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, g_readbackPBOs[i]);
        glBufferData(GL_PIXEL_PACK_BUFFER, stereoFrameSize, nullptr, GL_STREAM_READ);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    std::cout << "Created readback PBOs for async framebuffer read (stereo size: " << stereoFrameSize << " bytes)" << std::endl;

    // Create framebuffer for rendering
    glGenFramebuffers(1, &g_framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, g_framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_stereoTexture, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "Framebuffer not complete: " << status << std::endl;
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    std::cout << "Created stereo texture: " << (width * 2) << "x" << height << std::endl;
    return true;
}

bool createQuadGeometry() {
    // Full-screen quad vertices (position + texcoord)
    // Create quad that fills the entire viewport (-1 to 1 in NDC)
    // The viewport will handle aspect ratio, so keep quad square
    float quadVertices[] = {
        // Positions        // TexCoords
        -1.0f,  1.0f, 0.0f,  0.0f, 1.0f,  // Top-left
        -1.0f, -1.0f, 0.0f,  0.0f, 0.0f,  // Bottom-left
         1.0f, -1.0f, 0.0f,  1.0f, 0.0f,  // Bottom-right

        -1.0f,  1.0f, 0.0f,  0.0f, 1.0f,  // Top-left
         1.0f, -1.0f, 0.0f,  1.0f, 0.0f,  // Bottom-right
         1.0f,  1.0f, 0.0f,  1.0f, 1.0f   // Top-right
    };

    glGenVertexArrays(1, &g_vao);
    glGenBuffers(1, &g_vbo);

    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

    // Position attribute
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // TexCoord attribute
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

    return true;
}

// NOTE: Capture threading is now handled internally by CameraCapture class
// Each camera runs in its own dedicated thread with grab() loops to prevent buffer buildup
// This solves the bursting issue caused by cameras with different internal clocks

// Helper function to make sliders keyboard-friendly
// Arrow keys work immediately when slider is focused (via Tab) without needing to press Enter
template<typename T>
bool KeyboardFriendlySlider(const char* label, T* value, T min, T max, const char* format, ImGuiSliderFlags flags = 0) {
    // Static variable to track which slider processed input this frame (prevent multiple sliders from responding)
    static ImGuiID lastProcessedID = 0;
    static int lastProcessedFrame = -1;

    // Prevent click-to-jump: track blocked sliders across frames
    static ImGuiID blockedSliderID = 0;

    T oldValue = *value;

    // Draw the slider
    bool changed = false;
    if constexpr (std::is_same_v<T, float>) {
        changed = ImGui::SliderFloat(label, value, min, max, format, flags);
    } else if constexpr (std::is_same_v<T, int>) {
        changed = ImGui::SliderInt(label, value, min, max, format, flags);
    }

    ImGuiID thisID = ImGui::GetItemID();

    // On first click, check if it hit the grab handle
    if (ImGui::IsItemActivated()) {
        float sliderWidth = ImGui::CalcItemWidth();
        float sliderStartX = ImGui::GetItemRectMin().x;
        float grabNorm = (float)(oldValue - min) / (float)(max - min);
        float grabX = sliderStartX + grabNorm * sliderWidth;
        float clickX = ImGui::GetIO().MouseClickedPos[0].x;
        float grabTolerance = ImGui::GetStyle().GrabMinSize * 0.5f + 4.0f;

        if (fabsf(clickX - grabX) > grabTolerance) {
            blockedSliderID = thisID;
        }
    }

    // While blocked, keep reverting every frame until mouse released
    if (blockedSliderID == thisID) {
        if (ImGui::IsItemActive()) {
            *value = oldValue;
            changed = false;
        } else {
            blockedSliderID = 0;
        }
    }

    // Get current item ID and frame count
    ImGuiID currentID = ImGui::GetItemID();
    int currentFrame = ImGui::GetFrameCount();

    // Reset frame guard if this is a new frame
    if (currentFrame != lastProcessedFrame) {
        lastProcessedID = 0;
        lastProcessedFrame = currentFrame;
    }

    // If slider is focused (via Tab) but not active (not clicked), allow arrow keys to adjust value
    // Only process if no other slider has already processed input this frame
    if (ImGui::IsItemFocused() && !ImGui::IsItemActive() && lastProcessedID == 0) {
        // Calculate base step size based on format precision
        T baseStep;
        if constexpr (std::is_same_v<T, float>) {
            // Parse format string to determine precision (e.g., "%.2f" -> 0.01, "%.1f" -> 0.1)
            baseStep = 0.01f;  // Default
            if (format) {
                const char* dot = strchr(format, '.');
                if (dot && *(dot + 1) >= '0' && *(dot + 1) <= '9') {
                    int precision = *(dot + 1) - '0';
                    baseStep = 1.0f;
                    for (int i = 0; i < precision; i++) {
                        baseStep *= 0.1f;
                    }
                }
            }
        } else if constexpr (std::is_same_v<T, int>) {
            baseStep = 1;  // Always increment by 1 for integers
        }

        // Check for modifier keys to adjust step size
        ImGuiIO& io = ImGui::GetIO();
        T step = baseStep;

        if (io.KeyShift) {
            // Shift = fine adjustment (10x smaller steps)
            step = baseStep * 0.1f;
        } else if (io.KeyCtrl) {
            // Ctrl = coarse adjustment (10x larger steps)
            step = baseStep * 10.0f;
        }
        // No modifier = normal step

        // Handle arrow keys
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
            *value = (*value - step < min) ? min : *value - step;
            changed = true;
            lastProcessedID = currentID;  // Mark this slider as having processed input
        } else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
            *value = (*value + step > max) ? max : *value + step;
            changed = true;
            lastProcessedID = currentID;  // Mark this slider as having processed input
        }
    }

    return changed;
}

// D-pad widget for pixel offset adjustment (game controller style)
// Each click moves 1 pixel. Hold to auto-repeat.
bool DPadWidget(const char* label, int* valueX, int* valueY,
                int minX, int maxX, int minY, int maxY) {
    bool changed = false;
    ImGui::PushID(label);

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // Layout constants
    const float btnSize = 28.0f;       // Each arrow button size
    const float gap = 2.0f;            // Gap between buttons and center
    const float centerSize = 26.0f;    // Center circle diameter
    const float totalSize = btnSize * 3.0f + gap * 2.0f;
    const float rounding = 4.0f;

    // Colors matching dark controller aesthetic
    const ImU32 colBase     = IM_COL32(50, 50, 55, 255);
    const ImU32 colHover    = IM_COL32(70, 70, 78, 255);
    const ImU32 colActive   = IM_COL32(40, 40, 45, 255);
    const ImU32 colBorder   = IM_COL32(35, 35, 40, 255);
    const ImU32 colArrow    = IM_COL32(160, 160, 170, 255);
    const ImU32 colArrowDim = IM_COL32(90, 90, 100, 255);
    const ImU32 colBg       = IM_COL32(28, 28, 32, 255);
    const ImU32 colCenterBg = IM_COL32(38, 38, 42, 255);
    const ImU32 colCross    = IM_COL32(55, 55, 60, 255);

    ImVec2 origin = ImGui::GetCursorScreenPos();
    float cx = origin.x + totalSize * 0.5f;
    float cy = origin.y + totalSize * 0.5f;

    // Reserve space
    ImGui::Dummy(ImVec2(totalSize, totalSize));

    // --- Background: rounded dark plate ---
    drawList->AddRectFilled(origin, ImVec2(origin.x + totalSize, origin.y + totalSize),
                            colBg, 10.0f);
    drawList->AddRect(origin, ImVec2(origin.x + totalSize, origin.y + totalSize),
                      colBorder, 10.0f, 0, 1.5f);

    // --- Cross shape behind the buttons (the + shaped ridge) ---
    float crossW = btnSize + gap * 2.0f;
    // Vertical bar
    drawList->AddRectFilled(
        ImVec2(cx - crossW * 0.5f, origin.y + 2),
        ImVec2(cx + crossW * 0.5f, origin.y + totalSize - 2),
        colCross, rounding);
    // Horizontal bar
    drawList->AddRectFilled(
        ImVec2(origin.x + 2, cy - crossW * 0.5f),
        ImVec2(origin.x + totalSize - 2, cy + crossW * 0.5f),
        colCross, rounding);

    // --- Helper: draw one D-pad button and return if it was clicked ---
    // Uses ImGui repeat for hold-to-move
    struct DPadBtn {
        static bool draw(ImDrawList* dl, const char* id, ImVec2 pos, float size,
                         float rnd, int arrowDir, bool atLimit,
                         ImU32 base, ImU32 hover, ImU32 active, ImU32 border,
                         ImU32 arrow, ImU32 arrowDim) {
            ImGui::SetCursorScreenPos(pos);
            ImGui::PushButtonRepeat(true);
            bool pressed = ImGui::InvisibleButton(id, ImVec2(size, size));
            ImGui::PopButtonRepeat();
            bool isHov = ImGui::IsItemHovered();
            bool isAct = ImGui::IsItemActive();

            // Button fill
            ImU32 col = isAct ? active : (isHov ? hover : base);
            dl->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size), col, rnd);
            dl->AddRect(pos, ImVec2(pos.x + size, pos.y + size), border, rnd, 0, 1.0f);

            // Arrow triangle
            float mid = size * 0.5f;
            float arrSize = size * 0.28f;
            ImVec2 c(pos.x + mid, pos.y + mid);
            ImU32 arrCol = atLimit ? arrowDim : arrow;

            ImVec2 p1, p2, p3;
            switch (arrowDir) {
                case 0: // Up
                    p1 = ImVec2(c.x, c.y - arrSize);
                    p2 = ImVec2(c.x - arrSize, c.y + arrSize * 0.5f);
                    p3 = ImVec2(c.x + arrSize, c.y + arrSize * 0.5f);
                    break;
                case 1: // Down
                    p1 = ImVec2(c.x, c.y + arrSize);
                    p2 = ImVec2(c.x - arrSize, c.y - arrSize * 0.5f);
                    p3 = ImVec2(c.x + arrSize, c.y - arrSize * 0.5f);
                    break;
                case 2: // Left
                    p1 = ImVec2(c.x - arrSize, c.y);
                    p2 = ImVec2(c.x + arrSize * 0.5f, c.y - arrSize);
                    p3 = ImVec2(c.x + arrSize * 0.5f, c.y + arrSize);
                    break;
                case 3: // Right
                    p1 = ImVec2(c.x + arrSize, c.y);
                    p2 = ImVec2(c.x - arrSize * 0.5f, c.y - arrSize);
                    p3 = ImVec2(c.x - arrSize * 0.5f, c.y + arrSize);
                    break;
            }
            dl->AddTriangleFilled(p1, p2, p3, arrCol);
            return pressed;
        }
    };

    // --- Four directional buttons ---
    // Up
    if (DPadBtn::draw(drawList, "##up",
            ImVec2(cx - btnSize * 0.5f, cy - btnSize * 0.5f - gap - btnSize),
            btnSize, rounding, 0, *valueY <= minY,
            colBase, colHover, colActive, colBorder, colArrow, colArrowDim)) {
        if (*valueY > minY) { (*valueY)--; changed = true; }
    }
    // Down
    if (DPadBtn::draw(drawList, "##dn",
            ImVec2(cx - btnSize * 0.5f, cy + btnSize * 0.5f + gap),
            btnSize, rounding, 1, *valueY >= maxY,
            colBase, colHover, colActive, colBorder, colArrow, colArrowDim)) {
        if (*valueY < maxY) { (*valueY)++; changed = true; }
    }
    // Left
    if (DPadBtn::draw(drawList, "##lt",
            ImVec2(cx - btnSize * 0.5f - gap - btnSize, cy - btnSize * 0.5f),
            btnSize, rounding, 2, *valueX <= minX,
            colBase, colHover, colActive, colBorder, colArrow, colArrowDim)) {
        if (*valueX > minX) { (*valueX)--; changed = true; }
    }
    // Right
    if (DPadBtn::draw(drawList, "##rt",
            ImVec2(cx + btnSize * 0.5f + gap, cy - btnSize * 0.5f),
            btnSize, rounding, 3, *valueX >= maxX,
            colBase, colHover, colActive, colBorder, colArrow, colArrowDim)) {
        if (*valueX < maxX) { (*valueX)++; changed = true; }
    }

    // --- Center circle (recessed look) ---
    drawList->AddCircleFilled(ImVec2(cx, cy), centerSize * 0.5f, colCenterBg, 24);
    drawList->AddCircle(ImVec2(cx, cy), centerSize * 0.5f, colBorder, 24, 1.5f);

    // Value readout below
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + totalSize + 4));
    ImGui::Text("%s  X: %d  Y: %d", label, *valueX, *valueY);

    ImGui::PopID();
    return changed;
}

// Float D-pad for overlay position (1mm per click)
bool DPadWidgetFloat(const char* label, float* valueX, float* valueY,
                     float minX, float maxX, float minY, float maxY, float step = 0.001f) {
    bool changed = false;
    ImGui::PushID(label);

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    const float btnSize = 28.0f;
    const float gap = 2.0f;
    const float centerSize = 26.0f;
    const float totalSize = btnSize * 3.0f + gap * 2.0f;
    const float rounding = 4.0f;

    const ImU32 colBase     = IM_COL32(50, 50, 55, 255);
    const ImU32 colHover    = IM_COL32(70, 70, 78, 255);
    const ImU32 colActive   = IM_COL32(40, 40, 45, 255);
    const ImU32 colBorder   = IM_COL32(35, 35, 40, 255);
    const ImU32 colArrow    = IM_COL32(160, 160, 170, 255);
    const ImU32 colArrowDim = IM_COL32(90, 90, 100, 255);
    const ImU32 colBg       = IM_COL32(28, 28, 32, 255);
    const ImU32 colCenterBg = IM_COL32(38, 38, 42, 255);
    const ImU32 colCross    = IM_COL32(55, 55, 60, 255);

    ImVec2 origin = ImGui::GetCursorScreenPos();
    float cx = origin.x + totalSize * 0.5f;
    float cy = origin.y + totalSize * 0.5f;

    ImGui::Dummy(ImVec2(totalSize, totalSize));

    // Background plate
    drawList->AddRectFilled(origin, ImVec2(origin.x + totalSize, origin.y + totalSize), colBg, 10.0f);
    drawList->AddRect(origin, ImVec2(origin.x + totalSize, origin.y + totalSize), colBorder, 10.0f, 0, 1.5f);

    // Cross shape
    float crossW = btnSize + gap * 2.0f;
    drawList->AddRectFilled(ImVec2(cx - crossW * 0.5f, origin.y + 2),
                            ImVec2(cx + crossW * 0.5f, origin.y + totalSize - 2), colCross, rounding);
    drawList->AddRectFilled(ImVec2(origin.x + 2, cy - crossW * 0.5f),
                            ImVec2(origin.x + totalSize - 2, cy + crossW * 0.5f), colCross, rounding);

    // D-pad button helper (same as int version)
    struct Btn {
        static bool draw(ImDrawList* dl, const char* id, ImVec2 pos, float size,
                         float rnd, int dir, bool atLimit,
                         ImU32 base, ImU32 hover, ImU32 active, ImU32 border,
                         ImU32 arrow, ImU32 arrowDim) {
            ImGui::SetCursorScreenPos(pos);
            ImGui::PushButtonRepeat(true);
            bool pressed = ImGui::InvisibleButton(id, ImVec2(size, size));
            ImGui::PopButtonRepeat();
            bool isHov = ImGui::IsItemHovered();
            bool isAct = ImGui::IsItemActive();

            ImU32 col = isAct ? active : (isHov ? hover : base);
            dl->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size), col, rnd);
            dl->AddRect(pos, ImVec2(pos.x + size, pos.y + size), border, rnd, 0, 1.0f);

            float mid = size * 0.5f;
            float a = size * 0.28f;
            ImVec2 c(pos.x + mid, pos.y + mid);
            ImU32 ac = atLimit ? arrowDim : arrow;
            ImVec2 p1, p2, p3;
            switch (dir) {
                case 0: p1={c.x,c.y-a}; p2={c.x-a,c.y+a*0.5f}; p3={c.x+a,c.y+a*0.5f}; break;
                case 1: p1={c.x,c.y+a}; p2={c.x-a,c.y-a*0.5f}; p3={c.x+a,c.y-a*0.5f}; break;
                case 2: p1={c.x-a,c.y}; p2={c.x+a*0.5f,c.y-a}; p3={c.x+a*0.5f,c.y+a}; break;
                case 3: p1={c.x+a,c.y}; p2={c.x-a*0.5f,c.y-a}; p3={c.x-a*0.5f,c.y+a}; break;
            }
            dl->AddTriangleFilled(p1, p2, p3, ac);
            return pressed;
        }
    };

    // Up (Y+)
    if (Btn::draw(drawList, "##up", ImVec2(cx - btnSize*0.5f, cy - btnSize*0.5f - gap - btnSize),
            btnSize, rounding, 0, *valueY >= maxY,
            colBase, colHover, colActive, colBorder, colArrow, colArrowDim)) {
        if (*valueY < maxY) { *valueY += step; if (*valueY > maxY) *valueY = maxY; changed = true; }
    }
    // Down (Y-)
    if (Btn::draw(drawList, "##dn", ImVec2(cx - btnSize*0.5f, cy + btnSize*0.5f + gap),
            btnSize, rounding, 1, *valueY <= minY,
            colBase, colHover, colActive, colBorder, colArrow, colArrowDim)) {
        if (*valueY > minY) { *valueY -= step; if (*valueY < minY) *valueY = minY; changed = true; }
    }
    // Left (X-)
    if (Btn::draw(drawList, "##lt", ImVec2(cx - btnSize*0.5f - gap - btnSize, cy - btnSize*0.5f),
            btnSize, rounding, 2, *valueX <= minX,
            colBase, colHover, colActive, colBorder, colArrow, colArrowDim)) {
        if (*valueX > minX) { *valueX -= step; if (*valueX < minX) *valueX = minX; changed = true; }
    }
    // Right (X+)
    if (Btn::draw(drawList, "##rt", ImVec2(cx + btnSize*0.5f + gap, cy - btnSize*0.5f),
            btnSize, rounding, 3, *valueX >= maxX,
            colBase, colHover, colActive, colBorder, colArrow, colArrowDim)) {
        if (*valueX < maxX) { *valueX += step; if (*valueX > maxX) *valueX = maxX; changed = true; }
    }

    // Center circle
    drawList->AddCircleFilled(ImVec2(cx, cy), centerSize * 0.5f, colCenterBg, 24);
    drawList->AddCircle(ImVec2(cx, cy), centerSize * 0.5f, colBorder, 24, 1.5f);

    // Value readout
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + totalSize + 4));
    ImGui::Text("%s  X: %.3f  Y: %.3f", label, *valueX, *valueY);

    ImGui::PopID();
    return changed;
}

// Auto-calibrate chroma key settings by analyzing the current frame
void autoCalibrateChromaKey(const cv::Mat& bgrFrame, bool isLeftEye) {
    if (bgrFrame.empty()) return;

    Config& config = Config::getInstance();

    // Convert BGR to HSV for analysis
    cv::Mat hsvFrame;
    cv::cvtColor(bgrFrame, hsvFrame, cv::COLOR_BGR2HSV);

    // Sample center region (avoid edges which might have shadows/artifacts)
    int sampleWidth = bgrFrame.cols / 2;
    int sampleHeight = bgrFrame.rows / 2;
    int startX = (bgrFrame.cols - sampleWidth) / 2;
    int startY = (bgrFrame.rows - sampleHeight) / 2;
    cv::Rect sampleRegion(startX, startY, sampleWidth, sampleHeight);
    cv::Mat sampleHSV = hsvFrame(sampleRegion);

    // Calculate mean and standard deviation of HSV values
    cv::Scalar mean, stddev;
    cv::meanStdDev(sampleHSV, mean, stddev);

    // Extract HSV statistics
    float meanHue = mean[0] * 2.0f;  // OpenCV hue is 0-179, convert to 0-358
    float stdHue = stddev[0] * 2.0f;
    float meanSat = mean[1] / 255.0f;  // Convert to 0-1 range
    float stdSat = stddev[1] / 255.0f;
    float meanVal = mean[2] / 255.0f;

    // Calculate optimal chroma key parameters
    float hueCenter = meanHue;
    float hueRange = stdHue * 3.0f;  // 3 sigma covers ~99.7% of values
    hueRange = (hueRange < 30.0f) ? 30.0f : hueRange;  // Minimum range
    hueRange = (hueRange > 120.0f) ? 120.0f : hueRange;  // Maximum range

    float saturationMin = meanSat - stdSat * 2.0f;  // 2 sigma lower bound
    saturationMin = (saturationMin < 0.0f) ? 0.0f : saturationMin;
    saturationMin = (saturationMin > 0.8f) ? 0.8f : saturationMin;

    float valueMin = meanVal - stddev[2] / 255.0f * 2.0f;
    valueMin = (valueMin < 0.0f) ? 0.0f : valueMin;
    valueMin = (valueMin > 0.8f) ? 0.8f : valueMin;

    // Apply settings to config
    if (config.chromaKey.inverseMode) {
        if (isLeftEye) {
            config.chromaKey.leftHueCenterInverse = hueCenter;
            config.chromaKey.leftHueRangeInverse = hueRange;
            config.chromaKey.leftSaturationMinInverse = saturationMin;
            config.chromaKey.leftValueMinInverse = valueMin;
            config.chromaKey.leftEdgeSoftnessInverse = 0.05f;  // Default smooth edges
        } else {
            config.chromaKey.rightHueCenterInverse = hueCenter;
            config.chromaKey.rightHueRangeInverse = hueRange;
            config.chromaKey.rightSaturationMinInverse = saturationMin;
            config.chromaKey.rightValueMinInverse = valueMin;
            config.chromaKey.rightEdgeSoftnessInverse = 0.05f;
        }
    } else {
        if (isLeftEye) {
            config.chromaKey.leftHueCenter = hueCenter;
            config.chromaKey.leftHueRange = hueRange;
            config.chromaKey.leftSaturationMin = saturationMin;
            config.chromaKey.leftValueMin = valueMin;
            config.chromaKey.leftEdgeSoftness = 0.05f;
        } else {
            config.chromaKey.rightHueCenter = hueCenter;
            config.chromaKey.rightHueRange = hueRange;
            config.chromaKey.rightSaturationMin = saturationMin;
            config.chromaKey.rightValueMin = valueMin;
            config.chromaKey.rightEdgeSoftness = 0.05f;
        }
    }

    std::cout << "\n=== Auto-Calibrated Chroma Key (" << (isLeftEye ? "Left" : "Right") << " Eye) ===" << std::endl;
    std::cout << "Hue Center: " << hueCenter << std::endl;
    std::cout << "Hue Range: " << hueRange << std::endl;
    std::cout << "Saturation Min: " << saturationMin << std::endl;
    std::cout << "Value Min: " << valueMin << std::endl;
}

// CPU-based chroma key processing (utilizes CPU headroom, reduces GPU load)
cv::Mat applyCPUChromaKey(const cv::Mat& bgrFrame, bool isLeftEye, const cv::Mat& precomputedAlpha = cv::Mat()) {
    Config& config = Config::getInstance();

    // Get HSV-based chroma key parameters
    float hueCenter, hueRange, saturationMin, valueMin, edgeSoftness;
    if (isLeftEye) {
        if (config.chromaKey.inverseMode) {
            hueCenter = config.chromaKey.leftHueCenterInverse;
            hueRange = config.chromaKey.leftHueRangeInverse;
            saturationMin = config.chromaKey.leftSaturationMinInverse;
            valueMin = config.chromaKey.leftValueMinInverse;
            edgeSoftness = config.chromaKey.leftEdgeSoftnessInverse;
        } else {
            hueCenter = config.chromaKey.leftHueCenter;
            hueRange = config.chromaKey.leftHueRange;
            saturationMin = config.chromaKey.leftSaturationMin;
            valueMin = config.chromaKey.leftValueMin;
            edgeSoftness = config.chromaKey.leftEdgeSoftness;
        }
    } else {
        if (config.chromaKey.inverseMode) {
            hueCenter = config.chromaKey.rightHueCenterInverse;
            hueRange = config.chromaKey.rightHueRangeInverse;
            saturationMin = config.chromaKey.rightSaturationMinInverse;
            valueMin = config.chromaKey.rightValueMinInverse;
            edgeSoftness = config.chromaKey.rightEdgeSoftnessInverse;
        } else {
            hueCenter = config.chromaKey.rightHueCenter;
            hueRange = config.chromaKey.rightHueRange;
            saturationMin = config.chromaKey.rightSaturationMin;
            valueMin = config.chromaKey.rightValueMin;
            edgeSoftness = config.chromaKey.rightEdgeSoftness;
        }
    }

    // Convert BGR to HSV for chroma keying
    cv::Mat hsvFrame;
    cv::cvtColor(bgrFrame, hsvFrame, cv::COLOR_BGR2HSV);

    // Create RGBA output frame
    cv::Mat rgbaFrame(bgrFrame.rows, bgrFrame.cols, CV_8UC4);

    // Smoothstep matching GLSL/HLSL semantics (used below to mirror ChromaKey.hlsl exactly)
    auto smoothstepGLSL = [](float edge0, float edge1, float x) -> float {
        float t = (edge1 != edge0) ? (x - edge0) / (edge1 - edge0) : 0.0f;
        t = (t < 0.0f) ? 0.0f : ((t > 1.0f) ? 1.0f : t);
        return t * t * (3.0f - 2.0f * t);
    };

    bool useSegmentation = !precomputedAlpha.empty();

    // Process each pixel
    for (int y = 0; y < bgrFrame.rows; ++y) {
        const cv::Vec3b* bgrRow = bgrFrame.ptr<cv::Vec3b>(y);
        const cv::Vec3b* hsvRow = hsvFrame.ptr<cv::Vec3b>(y);
        cv::Vec4b* rgbaRow = rgbaFrame.ptr<cv::Vec4b>(y);
        const uint8_t* alphaRow = useSegmentation ? precomputedAlpha.ptr<uint8_t>(y) : nullptr;

        for (int x = 0; x < bgrFrame.cols; ++x) {
            const cv::Vec3b& bgr = bgrRow[x];
            cv::Vec4b& rgba = rgbaRow[x];

            // Convert BGR to RGB and copy
            rgba[0] = bgr[2];  // R
            rgba[1] = bgr[1];  // G
            rgba[2] = bgr[0];  // B

            float alphaMultiplier;
            if (useSegmentation) {
                // AI Segmentation mode: alpha already computed by SegmentationEngine -
                // skip the HSV chroma-key math entirely, matching OVERLAY_PS's branch.
                alphaMultiplier = alphaRow[x] / 255.0f;
            } else {
                const cv::Vec3b& hsv = hsvRow[x];
                // HSV-based chroma key, ported from the production shaders/ChromaKey.hlsl (used by
                // the OpenXR API layer at actual flight time) so this local preview responds to the
                // same Hue Center / Hue Range / Saturation Min / Value Min / Edge Softness controls
                // and accurately reflects what you'll see in-headset. The previous version here only
                // used hueRange+edgeSoftness and silently ignored hueCenter/saturationMin/valueMin.
                float hueDeg = hsv[0] * 2.0f;   // OpenCV hue is 0-179 for 8-bit images; convert to degrees
                float sat = hsv[1] / 255.0f;
                float val = hsv[2] / 255.0f;

                float hueDiff = std::fabs(hueDeg - hueCenter);
                if (hueDiff > 180.0f) hueDiff = 360.0f - hueDiff;

                float hueMatch = smoothstepGLSL(hueRange + edgeSoftness, hueRange - edgeSoftness, hueDiff);
                float satMatch = smoothstepGLSL(saturationMin - edgeSoftness, saturationMin + edgeSoftness, sat);
                float valMatch = smoothstepGLSL(valueMin - edgeSoftness, valueMin + edgeSoftness, val);
                float keyAmount = hueMatch * satMatch * valMatch;

                // In normal mode: high keyAmount = transparent (remove target color)
                // In inverse mode: high keyAmount = opaque (keep target color)
                alphaMultiplier = config.chromaKey.inverseMode ? keyAmount : (1.0f - keyAmount);
            }

            // Apply border mask (gradient fade at edges)
            if (config.chromaKey.borderMaskSize > 0.0f) {
                // Calculate normalized coordinates (0.0 to 1.0)
                float normX = (float)x / bgrFrame.cols;
                float normY = (float)y / bgrFrame.rows;

                // Distance from edge (0.0 at edge, 0.5 at center)
                float distX = (normX < 0.5f) ? normX : (1.0f - normX);
                float distY = (normY < 0.5f) ? normY : (1.0f - normY);
                float edgeDist = (distX < distY) ? distX : distY;

                // Apply smooth gradient based on borderMaskSize and borderMaskSoftness
                float maskStart = config.chromaKey.borderMaskSize;
                float maskEnd = maskStart + config.chromaKey.borderMaskSoftness;

                float borderMask = 1.0f;
                if (edgeDist < maskStart) {
                    borderMask = 0.0f;
                } else if (edgeDist < maskEnd) {
                    // Smooth transition
                    borderMask = (edgeDist - maskStart) / (maskEnd - maskStart);
                }

                alphaMultiplier *= borderMask;
            }

            // Set alpha channel (0-255)
            rgba[3] = static_cast<uint8_t>(alphaMultiplier * 255.0f);
        }
    }

    return rgbaFrame;
}

void renderCameraToTexture(const cv::Mat& frame, GLuint cameraTexture, GLuint pbo, int offsetX, int viewportWidth, int viewportHeight, bool isLeftEye) {
    // === OPTIMIZED TEXTURE UPLOAD USING PBO (ASYNC) ===
    // Step 1: Bind PBO and map buffer for writing
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);

    // Orphan the old buffer to avoid synchronization (driver allocates new memory)
    // FIX: Allocate buffer for RGBA data (4 channels after CPU chroma key)
    size_t packedSize = frame.cols * frame.rows * frame.channels();
    glBufferData(GL_PIXEL_UNPACK_BUFFER, packedSize, nullptr, GL_STREAM_DRAW);

    // Map buffer and copy frame data
    void* pboMemory = glMapBuffer(GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY);
    if (pboMemory) {
        // Check if Mat is continuous; if not, copy row-by-row to remove padding
        if (frame.isContinuous()) {
            memcpy(pboMemory, frame.data, packedSize);
        } else {
            // Row-by-row copy to handle padding (RGBA = 4 bytes per pixel)
            size_t rowSize = frame.cols * frame.channels();
            for (int i = 0; i < frame.rows; ++i) {
                memcpy((uint8_t*)pboMemory + i * rowSize, frame.ptr(i), rowSize);
            }
        }
        glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
    }

    // Step 2: Upload from PBO to texture (async, non-blocking)
    glBindTexture(GL_TEXTURE_2D, cameraTexture);

    // CRITICAL: Set pixel store parameters for packed data (no padding)
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);  // OpenCV uses 1-byte alignment
    glPixelStorei(GL_UNPACK_ROW_LENGTH, frame.cols);  // Crucial: width without padding

    // Upload RGBA (chroma key already applied on CPU)
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frame.cols, frame.rows, GL_RGBA, GL_UNSIGNED_BYTE, 0);  // 0 = use bound PBO

    // Reset to default
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    // Unbind PBO
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    // Set viewport for this half of the stereo texture
    glViewport(offsetX, 0, viewportWidth, viewportHeight);

    // Use simple passthrough shader (no GPU chroma key processing needed)
    g_previewShader.use();
    g_previewShader.setUniform1i("uTexture", 0);
    g_previewShader.setUniform1i("uFlipVertical", 1);  // Flip for DirectX compatibility

    // FIX: Both cameras are mirrored in raw output, so flip both in shader
    g_previewShader.setUniform1i("uFlipHorizontal", 1);  // Both eyes: flip to fix mirroring

    // Draw quad (alpha blending handled by pre-calculated alpha channel)
    glBindVertexArray(g_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    g_previewShader.unuse();
    glBindTexture(GL_TEXTURE_2D, 0);
}

bool processAndRenderFrames(const cv::Mat& leftFrame, const cv::Mat& rightFrame,
                            const cv::Mat& leftAlpha = cv::Mat(), const cv::Mat& rightAlpha = cv::Mat()) {
    Config& config = Config::getInstance();

    // === ZERO-COPY GPU TRANSFER: Lock texture for OpenGL write ===
    if (g_wglInteropDevice && g_wglInteropTexture) {
        // Lock the DirectX texture so OpenGL can write to it
        if (!wglDXLockObjectsNV(g_wglInteropDevice, 1, &g_wglInteropTexture)) {
            std::cerr << "[WGLInterop] Failed to lock texture!" << std::endl;
            // Fall through to rendering anyway (texture might still work)
        }
    }

    // Bind framebuffer to render to stereo texture
    glBindFramebuffer(GL_FRAMEBUFFER, g_framebuffer);

    // Clear the entire framebuffer
    glViewport(0, 0, config.camera.frameWidth * 2, config.camera.frameHeight);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Apply CPU-based chroma keying (utilizes CPU headroom, reduces GPU load)
    cv::Mat leftFrameRGBA = applyCPUChromaKey(leftFrame, true, leftAlpha);
    cv::Mat rightFrameRGBA = applyCPUChromaKey(rightFrame, false, rightAlpha);

    // Render left eye to left half (RGBA with pre-calculated alpha)
    glActiveTexture(GL_TEXTURE0);
    renderCameraToTexture(leftFrameRGBA, g_leftCameraTexture, g_leftPBO, 0, config.camera.frameWidth, config.camera.frameHeight, true);

    // Render right eye to right half (RGBA with pre-calculated alpha)
    renderCameraToTexture(rightFrameRGBA, g_rightCameraTexture, g_rightPBO, config.camera.frameWidth, config.camera.frameWidth, config.camera.frameHeight, false);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // === ZERO-COPY GPU TRANSFER: Unlock texture for DirectX read ===
    if (g_wglInteropDevice && g_wglInteropTexture) {
        // Unlock so DirectX/OpenVR can read the texture
        if (!wglDXUnlockObjectsNV(g_wglInteropDevice, 1, &g_wglInteropTexture)) {
            std::cerr << "[WGLInterop] Failed to unlock texture!" << std::endl;
        }
    } else {
        // Fallback: Use old CPU roundtrip method (will cause stalls)
        copyOpenGLToDirectX();
    }

    return true;
}

/**
 * @brief Tracking thread function - runs independently from rendering
 *
 * Continuously updates head tracking and writes atomic coordinates.
 * Main thread reads these coordinates without blocking.
 */
void trackingThreadFunc() {
    std::cout << "[TrackingThread] Started" << std::endl;

    while (g_trackingRunning.load(std::memory_order_acquire)) {
        // Update VR overlay position (reads HMD pose, calculates compensation)
        g_vrOverlay.updateWorldPosition();

        // Sleep to limit tracking rate (120 Hz is more than enough)
        std::this_thread::sleep_for(std::chrono::microseconds(8333)); // ~120 Hz
    }

    std::cout << "[TrackingThread] Exiting" << std::endl;
}

void mainLoop() {
    // Frames are now fetched directly from per-camera threads (no FrameData needed here)
    auto lastFrameTime = std::chrono::steady_clock::now();
    auto lastStatsTime = std::chrono::steady_clock::now();
    int frameCount = 0;

    Config& config = Config::getInstance();

    std::cout << "Entering main loop..." << std::endl;
    std::cout << "Overlay locked to cockpit (seated space)" << std::endl;
    std::cout << "Press 'C' to reload config" << std::endl;
    std::cout << "Use 'Overlay Position' sliders to adjust position/rotation" << std::endl;
    std::cout << "Close the window to exit" << std::endl;
    std::cout << "NOTE: Video feed continues even when window is minimized or not in focus" << std::endl;

    // Check for joystick
    int joystickID = config.input.joystickID;

    // List all available joysticks first
    std::cout << "\n=== Available Joysticks ===" << std::endl;
    bool foundAny = false;
    for (int i = GLFW_JOYSTICK_1; i <= GLFW_JOYSTICK_LAST; i++) {
        if (glfwJoystickPresent(i)) {
            const char* name = glfwGetJoystickName(i);
            int axesCount, buttonCount;
            glfwGetJoystickAxes(i, &axesCount);
            glfwGetJoystickButtons(i, &buttonCount);
            std::cout << "ID " << i << ": " << name
                      << " (Axes: " << axesCount << ", Buttons: " << buttonCount << ")" << std::endl;
            foundAny = true;
        }
    }
    if (!foundAny) {
        std::cout << "No joysticks detected" << std::endl;
    }
    std::cout << "===========================\n" << std::endl;

    // If joystickID is -1 (auto-detect), find first available joystick
    if (joystickID == -1) {
        for (int i = GLFW_JOYSTICK_1; i <= GLFW_JOYSTICK_LAST; i++) {
            if (glfwJoystickPresent(i)) {
                joystickID = i;
                const char* name = glfwGetJoystickName(i);
                std::cout << "Auto-detected: Using joystick ID " << i << " (" << name << ")" << std::endl;
                std::cout << "Using button " << config.input.joystickResetButton << " for reset" << std::endl;
                std::cout << "To use a different joystick, set JoystickID in settings.ini\n" << std::endl;
                break;
            }
        }
    } else {
        // Verify the specified joystick ID is present
        if (glfwJoystickPresent(joystickID)) {
            const char* name = glfwGetJoystickName(joystickID);
            std::cout << "Using configured joystick ID " << joystickID << " (" << name << ")" << std::endl;
            std::cout << "Using button " << config.input.joystickResetButton << " for reset\n" << std::endl;
        } else {
            std::cerr << "Warning: Configured joystick ID " << joystickID << " not found!" << std::endl;
            std::cerr << "Falling back to auto-detect mode\n" << std::endl;
            joystickID = -1;
            // Try to find first available
            for (int i = GLFW_JOYSTICK_1; i <= GLFW_JOYSTICK_LAST; i++) {
                if (glfwJoystickPresent(i)) {
                    joystickID = i;
                    break;
                }
            }
        }
    }

    bool rKeyWasPressed = false;
    bool cKeyWasPressed = false;
    bool joystickButtonWasPressed = false;
    bool configChanged = false;  // Track if GUI sliders changed config values
    auto lastConfigChangeTime = std::chrono::steady_clock::now();
    bool configDirty = false;  // True when unsaved changes exist
    bool wasIconified = false;  // Track iconified state to log transitions
    bool hadFocus = true;  // Track window focus state

    // Frame sequence tracking - only process NEW frames (prevents duplicate processing)
    uint64_t lastLeftSeq = 0;
    uint64_t lastRightSeq = 0;

    // Frame rate limiting (reduce CPU/GPU load on MSFS)
    const double targetFrameTime = 1.0 / 20.0;  // 20 FPS cap (lower = less MSFS impact)
    auto lastLoopTime = std::chrono::steady_clock::now();

    while (!glfwWindowShouldClose(g_window)) {
        // Frame rate limiter - prevent excessive CPU/GPU usage
        auto currentLoopTime = std::chrono::steady_clock::now();
        double elapsedTime = std::chrono::duration<double>(currentLoopTime - lastLoopTime).count();

        if (elapsedTime < targetFrameTime) {
            // Sleep for remaining time to cap at 20 FPS (reduces MSFS impact)
            int sleepMs = static_cast<int>((targetFrameTime - elapsedTime) * 1000.0);
            if (sleepMs > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
            }
        }
        lastLoopTime = std::chrono::steady_clock::now();
        // Check for keyboard input
        glfwPollEvents();

        // Check if window is iconified (minimized)
        int isIconified = glfwGetWindowAttrib(g_window, GLFW_ICONIFIED);

        // Check if window has focus (important for detecting when MSFS takes focus)
        int hasFocus = glfwGetWindowAttrib(g_window, GLFW_FOCUSED);

        // Get framebuffer size to detect if window is minimized/invalid
        int windowWidth, windowHeight;
        glfwGetFramebufferSize(g_window, &windowWidth, &windowHeight);
        bool windowValid = (windowWidth > 0 && windowHeight > 0);

        // Log window state transitions
        if (isIconified && !wasIconified) {
            std::cout << "\n=== Window minimized - VR overlay continues running ===" << std::endl;
            std::cout << "Camera capture and VR updates continue in background" << std::endl;
            wasIconified = true;
        } else if (!isIconified && wasIconified) {
            std::cout << "\n=== Window restored ===" << std::endl;
            wasIconified = false;
        }

        // Log focus state transitions (detect when MSFS takes focus)
        if (!hasFocus && hadFocus) {
            std::cout << "\n=== Window lost focus (MSFS active?) - VR overlay continues ===" << std::endl;
            std::cout << "Camera capture, processing, and VR updates continue at full speed" << std::endl;
            hadFocus = false;
        } else if (hasFocus && !hadFocus) {
            std::cout << "\n=== Window regained focus ===" << std::endl;
            hadFocus = true;
        }

        // Check for reset input (keyboard or joystick button)
        bool resetPressed = false;
        bool rKeyPressed = (glfwGetKey(g_window, GLFW_KEY_9) == GLFW_PRESS);

        // Check joystick button (configurable)
        bool joystickButtonPressed = false;
        if (joystickID >= 0 && glfwJoystickPresent(joystickID)) {
            int buttonCount;
            const unsigned char* buttons = glfwGetJoystickButtons(joystickID, &buttonCount);
            int buttonIndex = config.input.joystickResetButton;
            if (buttonCount > buttonIndex && buttons[buttonIndex] == GLFW_PRESS) {
                joystickButtonPressed = true;
            }
        }

        // Reset button disabled - overlay is permanently locked to seated space
        // Position is set at startup and via sliders only
        resetPressed = (rKeyPressed && !rKeyWasPressed) || (joystickButtonPressed && !joystickButtonWasPressed);

        // if (resetPressed) {
        //     std::cout << "\n=== Reset button pressed - resetting position ===" << std::endl;
        //     g_vrOverlay.resetPosition();
        // }

        rKeyWasPressed = rKeyPressed;
        joystickButtonWasPressed = joystickButtonPressed;

        // Capacitive touch HID gamepad: must be polled here (main thread, after
        // glfwPollEvents() ran earlier this iteration) - see TouchInput.h. Queues any
        // newly-touched zones for the segmentation/matching block further down to consume.
        if (config.touch.enabled) {
            g_touchInput.update();
        }

        // C key to reload config
        bool cKeyPressed = (glfwGetKey(g_window, GLFW_KEY_C) == GLFW_PRESS);
        if (cKeyPressed && !cKeyWasPressed) {
            if (config.load("config/settings.ini")) {
                std::cout << "\n=== Config reloaded ===" << std::endl;
                std::cout << "SplitOffsetPixels: " << config.camera.splitOffsetPixels << std::endl;
                std::cout << "Width: " << config.overlay.width << "m, Height: " << config.overlay.height << "m" << std::endl;
                std::cout << "Position: (" << config.overlay.posX << ", " << config.overlay.posY << ", " << config.overlay.posZ << ")" << std::endl;
                std::cout << "Rotation: Pitch=" << config.overlay.pitch << ", Yaw=" << config.overlay.yaw << ", Roll=" << config.overlay.roll << std::endl;
                std::cout << "Opacity: " << config.overlay.opacity << std::endl;

                // Apply overlay settings
                g_vrOverlay.setOverlayTransform(config.overlay.width,
                                                config.overlay.height,
                                                config.overlay.distance,
                                                config.overlay.horizontalOffset,
                                                config.overlay.verticalOffset);
                g_vrOverlay.setOpacity(config.overlay.opacity);
                g_vrOverlay.resetPosition();
                g_pinActive = false;  // Clear pin state since position changed externally
                std::cout << "Overlay settings applied" << std::endl;
            } else {
                std::cerr << "Failed to reload config" << std::endl;
            }
        }
        cKeyWasPressed = cKeyPressed;

        // Check if window was closed
        if (glfwWindowShouldClose(g_window)) {
            break;
        }

        // === CRITICAL: Camera frame processing (only when new frames available) ===
        // If VR compositor hangs for 4 seconds, cameras accumulate 120 frames (30 FPS × 4s)
        // We MUST skip old frames and jump to the latest to prevent burst processing

        // Get current camera sequence numbers (without updating our tracking)
        uint64_t currentLeftSeq = g_cameras.getLeftSeq();
        uint64_t currentRightSeq = g_cameras.getRightSeq();

        // Calculate backlog (how many frames behind we are)
        int64_t leftBacklog = static_cast<int64_t>(currentLeftSeq) - static_cast<int64_t>(lastLeftSeq);
        int64_t rightBacklog = static_cast<int64_t>(currentRightSeq) - static_cast<int64_t>(lastRightSeq);
        int64_t maxBacklog = (leftBacklog > rightBacklog) ? leftBacklog : rightBacklog;

        // If backlog > 2 frames, we're falling behind - FLUSH old frames
        if (maxBacklog > 2) {
            // FLUSH: Jump sequence numbers to latest without processing old frames
            std::cout << "[FLUSH] Backlog detected! Skipping " << maxBacklog
                      << " frames (Left: " << leftBacklog << ", Right: " << rightBacklog << ")" << std::endl;
            lastLeftSeq = currentLeftSeq;
            lastRightSeq = currentRightSeq;
            // Now fall through to get the LATEST frame only
        }

        // Process new frames if available (non-blocking)
        // NOTE: This continues even when MSFS is in focus!
        if (g_cameras.hasNewFrames(lastLeftSeq, lastRightSeq)) {
            // Get latest frames from camera threads
            cv::Mat leftFrame, rightFrame;
            if (g_cameras.getLatestFrames(leftFrame, rightFrame)) {
                // Count processed frames
                g_perfStats.captureFrameCount.fetch_add(1, std::memory_order_relaxed);

                auto processStart = std::chrono::steady_clock::now();

                // Per-camera brightness/contrast, applied to the raw frame before anything
                // else touches it (chroma-key, AI segmentation, and the headset all see the
                // adjusted image) - a no-op at the defaults (contrast=1, brightness=0).
                if (config.camera.leftContrast != 1.0f || config.camera.leftBrightness != 0.0f) {
                    leftFrame.convertTo(leftFrame, -1, config.camera.leftContrast, config.camera.leftBrightness);
                }
                if (config.camera.rightContrast != 1.0f || config.camera.rightBrightness != 0.0f) {
                    rightFrame.convertTo(rightFrame, -1, config.camera.rightContrast, config.camera.rightBrightness);
                }

                // AI Segmentation: compute each eye's alpha matte once here, and reuse it
                // for both the shared-memory write (real headset path) and the CPU preview
                // below - never run inference twice for the same frame. Empty Mats (default)
                // mean "legacy Chroma Key mode" to both consumers.
                cv::Mat leftAlpha, rightAlpha;
                bool useSegmentation = (g_keyingMode == KeyingMode::AISegmentation) &&
                                       g_segmentationEngine.isReady();
                if (useSegmentation) {
                    // Push live-tunable preprocessing knobs each frame - cheap, and lets the
                    // UI sliders in the Chroma Key tab take effect immediately.
                    g_segmentationEngine.setAutoGainEnabled(config.segmentation.autoGainEnabled);
                    g_segmentationEngine.setAutoGainTarget(config.segmentation.autoGainTarget);
                    g_segmentationEngine.setAutoGainMaxGain(config.segmentation.autoGainMaxGain);
                    g_segmentationEngine.setClaheClipLimit(config.segmentation.claheClipLimit);

                    // Prefer GPU when MSFS isn't the focused/actively-rendering window (best
                    // quality, no contention to worry about); fall back to CPU when it is
                    // (avoids competing with MSFS's own rendering for the GPU). computeAlpha
                    // silently uses CPU regardless if the GPU session isn't available at all.
                    bool preferGpu = !isFlightSimulatorForeground();
                    auto segStart = std::chrono::steady_clock::now();
                    leftAlpha = g_segmentationEngine.computeAlpha(leftFrame, true, preferGpu);
                    rightAlpha = g_segmentationEngine.computeAlpha(rightFrame, false, preferGpu);
                    g_perfStats.avgSegmentationTimeMs.store(
                        std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - segStart).count(),
                        std::memory_order_relaxed);
                    if (leftAlpha.empty() || rightAlpha.empty()) {
                        // Inference failed for this frame - fall through to chroma-key
                        // for this frame only, rather than showing a broken/blank matte.
                        // Logged (throttled) since this failing on EVERY frame under some
                        // specific condition would look identical to a full mode switch.
                        static auto lastLogTime = std::chrono::steady_clock::now() - std::chrono::seconds(10);
                        auto now = std::chrono::steady_clock::now();
                        if (std::chrono::duration_cast<std::chrono::seconds>(now - lastLogTime).count() >= 2) {
                            std::cerr << "[AI Segmentation] computeAlpha failed this frame (preferGpu="
                                      << preferGpu << "): " << g_segmentationEngine.getLastError() << std::endl;
                            lastLogTime = now;
                        }
                        leftAlpha.release();
                        rightAlpha.release();
                    }
                }

                // Live hand diagnostics for the Touch Calibration tab: computed every frame
                // segmentation produces a usable mask, independent of any touch event, so
                // fingertip position and hand orientation/direction can be watched moving in
                // real time while tuning entry edge / verifying the pipeline sees a hand at all.
                if (useSegmentation) {
                    HandEntryEdge diagEdge = static_cast<HandEntryEdge>(config.touch.entryEdge);
                    g_liveHandDiag.fingertipLeft = findFingertipNormalized(leftAlpha, diagEdge);
                    g_liveHandDiag.fingertipRight = findFingertipNormalized(rightAlpha, diagEdge);

                    float orientLeft = computeHandOrientationAngle(leftAlpha);
                    float orientRight = computeHandOrientationAngle(rightAlpha);
                    if (std::isnan(orientLeft)) {
                        g_axisFilterLeftDiag.reset();  // no hand this frame - don't bias next detection
                        g_liveHandDiag.orientationLeftDeg = orientLeft;
                    } else {
                        float filtered = g_axisFilterLeftDiag.update(orientLeft, config.touch.axisFilterAlpha);
                        g_liveHandDiag.orientationLeftDeg = filtered * 180.0f / static_cast<float>(CV_PI);
                    }
                    if (std::isnan(orientRight)) {
                        g_axisFilterRightDiag.reset();
                        g_liveHandDiag.orientationRightDeg = orientRight;
                    } else {
                        float filtered = g_axisFilterRightDiag.update(orientRight, config.touch.axisFilterAlpha);
                        g_liveHandDiag.orientationRightDeg = filtered * 180.0f / static_cast<float>(CV_PI);
                    }

                    float dirLeft = computeHandDirectionAngle(leftAlpha, diagEdge);
                    float dirRight = computeHandDirectionAngle(rightAlpha, diagEdge);
                    g_liveHandDiag.directionLeftDeg = std::isnan(dirLeft) ? dirLeft
                        : dirLeft * 180.0f / static_cast<float>(CV_PI);
                    g_liveHandDiag.directionRightDeg = std::isnan(dirRight) ? dirRight
                        : dirRight * 180.0f / static_cast<float>(CV_PI);
                }

                // Capacitive touch: check for a pending event and, if segmentation produced a
                // usable mask this frame, resolve it to a fingertip position and either feed
                // calibration capture or match against the calibrated button table. Uses
                // *this* frame's mask rather than trying to align to the touch event's own
                // device timestamp - at ~30fps the mask is at most one frame stale relative
                // to the touch, well within the fingertip-vs-button matching tolerance.
                if (config.touch.enabled && useSegmentation && (!leftAlpha.empty() || !rightAlpha.empty())) {
                    TouchEvent touchEvt;
                    if (g_touchInput.pollEvent(touchEvt)) {
                        HandEntryEdge entryEdge = static_cast<HandEntryEdge>(config.touch.entryEdge);

                        if (g_touchCalibrationArmed) {
                            const cv::Mat& mask = g_touchCalibrationTargetEye ? leftAlpha : rightAlpha;
                            cv::Point2f fingertip = findFingertipNormalized(mask, entryEdge);
                            if (fingertip.x >= 0.0f && g_touchCalibrationTargetIndex >= 0 &&
                                g_touchCalibrationTargetIndex < static_cast<int>(config.touch.buttons.size())) {
                                auto& btn = config.touch.buttons[g_touchCalibrationTargetIndex];
                                btn.xNorm = fingertip.x;
                                btn.yNorm = fingertip.y;
                                btn.isLeftEye = g_touchCalibrationTargetEye;
                                btn.zone = touchEvt.zone;
                                std::cout << "[Touch Calibration] Captured '" << btn.name << "' at ("
                                          << fingertip.x << ", " << fingertip.y << "), zone "
                                          << touchEvt.zone << std::endl;
                                g_touchCalibrationArmed = false;
                                g_touchCalibrationTargetIndex = -1;
                            } else {
                                std::cout << "[Touch Calibration] No fingertip found this frame - try again" << std::endl;
                            }
                        } else {
                            // Try left eye first, then right - a calibration entry only exists
                            // for one eye per button, so whichever eye actually sees the hand
                            // contour is the one that will match.
                            cv::Point2f fingertipLeft = findFingertipNormalized(leftAlpha, entryEdge);
                            TouchMatchResult result = matchButton(fingertipLeft, true, touchEvt.zone,
                                                                   config.touch.buttons, config.touch.matchMaxDistNorm);
                            if (!result.matched) {
                                cv::Point2f fingertipRight = findFingertipNormalized(rightAlpha, entryEdge);
                                TouchMatchResult resultRight = matchButton(fingertipRight, false, touchEvt.zone,
                                                                            config.touch.buttons, config.touch.matchMaxDistNorm);
                                if (resultRight.matched || result.fingertipXNorm < 0.0f) {
                                    result = resultRight;
                                }
                            }

                            g_lastTouchMatch = result;
                            g_lastTouchMatchTime = std::chrono::steady_clock::now();

                            if (result.matched) {
                                std::cout << "[Touch] zone " << touchEvt.zone << " -> '" << result.buttonName
                                          << "' (" << (result.isLeftEye ? "left" : "right") << " eye, x="
                                          << result.fingertipXNorm << ", y=" << result.fingertipYNorm << ")" << std::endl;

                                const auto& matchedBtn = config.touch.buttons[result.buttonIndex];
                                if (matchedBtn.type == TouchControlType::Dial) {
                                    // A dial's position never changes no matter which way it's
                                    // turned - the initial position match above only confirms
                                    // *which* dial was touched. Rotation direction/amount comes
                                    // from tracking the hand's own twist every frame for as
                                    // long as the touch stays held (see the block below, right
                                    // after glfwPollEvents() reads the joystick each frame).
                                    const cv::Mat& dragMask = result.isLeftEye ? leftAlpha : rightAlpha;
                                    float startAngle = computeHandOrientationAngle(dragMask);
                                    if (!std::isnan(startAngle)) {
                                        g_activeDialDrag = ActiveDialDrag();  // fresh filter state too
                                        g_activeDialDrag.active = true;
                                        g_activeDialDrag.buttonIndex = result.buttonIndex;
                                        g_activeDialDrag.zone = touchEvt.zone;
                                        g_activeDialDrag.isLeftEye = result.isLeftEye;
                                        g_activeDialDrag.lastOrientationRad =
                                            g_activeDialDrag.filter.update(startAngle, config.touch.axisFilterAlpha);
                                        g_activeDialDrag.totalRotationRad = 0.0f;
                                        std::cout << "[Touch] Dial '" << matchedBtn.name
                                                  << "' - tracking rotation while held" << std::endl;
                                    }
                                }
                            } else {
                                std::cout << "[Touch] zone " << touchEvt.zone << " -> no button match ("
                                          << (result.isLeftEye ? "left" : "right") << " eye, x="
                                          << result.fingertipXNorm << ", y=" << result.fingertipYNorm << ")" << std::endl;
                            }
                        }
                    }

                    // Continuous dial-rotation tracking: runs every frame a drag is active,
                    // independent of whether a new touch event arrived this frame (a dial
                    // held and twisted doesn't generate new HID events - the button just
                    // stays pressed - so this has to sample the mask on its own each frame).
                    if (g_activeDialDrag.active) {
                        if (!g_touchInput.isHeld(g_activeDialDrag.zone)) {
                            const auto& dialBtn = config.touch.buttons[g_activeDialDrag.buttonIndex];
                            std::cout << "[Touch] Dial '" << dialBtn.name << "' released - final tick count "
                                      << g_activeDialDrag.tickCount << " (total "
                                      << (g_activeDialDrag.totalRotationRad * 180.0f / static_cast<float>(CV_PI))
                                      << " deg)" << std::endl;
                            g_activeDialDrag = ActiveDialDrag();
                        } else {
                            const cv::Mat& dragMask = g_activeDialDrag.isLeftEye ? leftAlpha : rightAlpha;
                            float rawAngle = computeHandOrientationAngle(dragMask);
                            if (!std::isnan(rawAngle)) {
                                float angle = g_activeDialDrag.filter.update(rawAngle, config.touch.axisFilterAlpha);
                                float delta = angleDeltaAxis(g_activeDialDrag.lastOrientationRad, angle);
                                g_activeDialDrag.lastOrientationRad = angle;
                                g_activeDialDrag.totalRotationRad += delta;

                                // Small dead-zone so mask noise between frames (when the hand
                                // is essentially still) doesn't spam ticks. A plain sign check
                                // against this threshold - not the magnitude of delta - is
                                // deliberately all this counts on: "did it move, and which way"
                                // is a much more reliable question to ask of this signal than
                                // "by how much", given the sensitivity limits found in testing.
                                constexpr float kNoiseThresholdRad = 0.02f;  // ~1.1 degrees
                                if (delta > kNoiseThresholdRad) {
                                    g_activeDialDrag.tickCount += 1;
                                    const auto& dialBtn = config.touch.buttons[g_activeDialDrag.buttonIndex];
                                    std::cout << "[Touch] Dial '" << dialBtn.name << "' tick +1 (count "
                                              << g_activeDialDrag.tickCount << ")" << std::endl;
                                } else if (delta < -kNoiseThresholdRad) {
                                    g_activeDialDrag.tickCount -= 1;
                                    const auto& dialBtn = config.touch.buttons[g_activeDialDrag.buttonIndex];
                                    std::cout << "[Touch] Dial '" << dialBtn.name << "' tick -1 (count "
                                              << g_activeDialDrag.tickCount << ")" << std::endl;
                                }
                            }
                        }
                    }
                }

                // Bake the active dial's tick-count bar directly into the camera frame +
                // alpha, before either goes to shared memory below - this reuses the
                // existing raw-frame/alpha VR pipeline entirely (no changes needed to the
                // injected OpenXR layer): whatever's drawn here and marked fully opaque
                // shows up in the headset exactly like the hand cutout does, since it's the
                // same texture the API layer already renders. Only meaningful in AI
                // Segmentation mode (leftAlpha/rightAlpha are only populated there), same
                // as the rest of touch/dial tracking.
                if (useSegmentation && g_activeDialDrag.active && leftFrame.data && rightFrame.data
                        && !leftAlpha.empty() && !rightAlpha.empty()) {
                    constexpr int kBarRangeTicksVR = 20;  // ticks shown as full deflection each way
                    float frac = static_cast<float>(g_activeDialDrag.tickCount) / static_cast<float>(kBarRangeTicksVR);
                    if (frac > 1.0f) frac = 1.0f;
                    if (frac < -1.0f) frac = -1.0f;

                    auto drawTickBar = [&](cv::Mat& frame, cv::Mat& alpha) {
                        int barWidth = static_cast<int>(frame.cols * 0.5f);
                        int barHeight = frame.rows / 30;
                        if (barHeight < 8) barHeight = 8;
                        int barX = (frame.cols - barWidth) / 2;
                        int barY = frame.rows / 40;
                        if (barY < 4) barY = 4;

                        cv::Rect bgRect(barX, barY, barWidth, barHeight);
                        cv::rectangle(frame, bgRect, cv::Scalar(40, 40, 40), cv::FILLED);  // dark background (BGR)
                        cv::rectangle(alpha, bgRect, cv::Scalar(255), cv::FILLED);  // fully opaque - always visible

                        int centerX = barX + barWidth / 2;
                        int fillX = centerX + static_cast<int>(frac * (barWidth / 2));
                        int fillLeft = (fillX < centerX) ? fillX : centerX;
                        int fillRight = (fillX < centerX) ? centerX : fillX;
                        cv::Rect fillRect(fillLeft, barY, fillRight - fillLeft, barHeight);
                        cv::Scalar fillColor = (frac >= 0.0f) ? cv::Scalar(80, 200, 80) : cv::Scalar(80, 80, 220);  // BGR
                        cv::rectangle(frame, fillRect, fillColor, cv::FILLED);

                        cv::line(frame, cv::Point(centerX, barY), cv::Point(centerX, barY + barHeight),
                                 cv::Scalar(255, 255, 255), 1);
                        cv::rectangle(frame, bgRect, cv::Scalar(200, 200, 200), 1);  // border
                    };

                    drawTickBar(leftFrame, leftAlpha);
                    drawTickBar(rightFrame, rightAlpha);
                }

                // Write RAW camera frames directly to shared memory (before CPU chroma key).
                // By default the API layer's GPU shader handles chroma keying in HSV space;
                // when leftAlpha/rightAlpha are non-empty, the precomputed alpha is written
                // instead and the API layer uses it as-is (see FrameHeader::alphaMode).
                if (leftFrame.data && rightFrame.data) {
                    g_vrOverlay.writeRawStereoFrame(
                        leftFrame.data, rightFrame.data,
                        leftFrame.cols, leftFrame.rows,
                        static_cast<int>(leftFrame.step[0]),
                        leftAlpha.empty() ? nullptr : leftAlpha.data,
                        leftAlpha.empty() ? 0 : static_cast<int>(leftAlpha.step[0]),
                        rightAlpha.empty() ? nullptr : rightAlpha.data,
                        rightAlpha.empty() ? 0 : static_cast<int>(rightAlpha.step[0]));
                    // Draw pin crosshair on the frame (after write, before API layer reads)
                    if (g_showCrosshair) {
                        g_vrOverlay.drawCrosshair(g_pinU, g_pinV);
                    }
                }

                // Only do CPU chroma key + OpenGL rendering when the preview window is visible
                // This saves significant CPU (~10%) when the window is minimized during flight
                if (!isIconified && windowValid && g_livePreviewEnabled) {
                    // Ensure OpenGL context is still valid
                    GLFWwindow* currentContext = glfwGetCurrentContext();
                    if (currentContext != g_window) {
                        std::cerr << "[WARNING] OpenGL context lost! Attempting to restore..." << std::endl;
                        glfwMakeContextCurrent(g_window);
                    }

                    // Process frames (CPU chroma key/AI segmentation + OpenGL rendering for preview)
                    if (processAndRenderFrames(leftFrame, rightFrame, leftAlpha, rightAlpha)) {
                        g_perfStats.renderFrameCount.fetch_add(1, std::memory_order_relaxed);
                    }
                }

                auto processEnd = std::chrono::steady_clock::now();
                double processMs = std::chrono::duration<double, std::milli>(
                    processEnd - processStart).count();
                g_perfStats.avgProcessTimeMs.store(processMs, std::memory_order_relaxed);
            }
        }

        // === Window rendering ===
        // NOTE: Camera processing and VR overlay updates continue regardless of window state
        // Even when minimized/iconified, we process frames and update VR - just skip window rendering

        // Start ImGui frame (must happen even when iconified to keep ImGui state valid)
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Only render to window if valid (not iconified and has non-zero size)
        if (!isIconified && windowValid) {
            // Render to GLFW window
            glBindFramebuffer(GL_FRAMEBUFFER, 0);

            glViewport(0, 0, windowWidth, windowHeight);
            glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }

        // TESTING ONLY: floating ImGui window showing the composited chroma-key texture so
        // it can be checked visually without SteamVR/MSFS. Normally this texture is only
        // consumed from shared memory by the OpenXR API layer inside a real VR session.
        // Gated behind a checkbox (default off) because the CPU chroma-key path it depends
        // on is expensive - see g_livePreviewEnabled above.
        //
        // NOTE: displays g_leftCameraTexture/g_rightCameraTexture, NOT g_stereoTexture.
        // g_stereoTexture is registered with wglDXRegisterObjectNV using WGL_ACCESS_WRITE_DISCARD_NV
        // (see initializeWGLInterop) - it's a write-only handle into a DirectX-owned resource for
        // VR submission, and sampling it back from OpenGL (e.g. via ImGui::Image) is undefined
        // behavior - it renders as solid black regardless of what was actually captured. The per-eye
        // camera textures are normal, independently-owned OpenGL textures and safe to sample.
        {
            float eyeW = (float)config.camera.frameWidth * 0.5f;
            float eyeH = (float)config.camera.frameHeight * 0.5f;
            ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(eyeW * 2 + 28, eyeH + 60), ImGuiCond_FirstUseEver);
            ImGui::Begin("Live Preview (TEST ONLY)");
            ImGui::Checkbox("Enable live preview (CPU-heavy)", &g_livePreviewEnabled);
            if (g_livePreviewEnabled) {
                ImGui::Image((ImTextureID)(intptr_t)g_leftCameraTexture, ImVec2(eyeW, eyeH),
                             ImVec2(0, 1), ImVec2(1, 0));
                ImGui::SameLine();
                ImGui::Image((ImTextureID)(intptr_t)g_rightCameraTexture, ImVec2(eyeW, eyeH),
                             ImVec2(0, 1), ImVec2(1, 0));
            } else {
                ImGui::TextDisabled("Preview off - check the box above to render it.");
            }
            ImGui::End();
        }

        // Main Control Panel GUI - Tabbed Interface
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(500, 650), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.95f);  // Make window more opaque
        ImGui::Begin("MSFS Hand Overlay Control Panel", nullptr, ImGuiWindowFlags_None);

        // Create tab bar
        if (ImGui::BeginTabBar("ControlTabs", ImGuiTabBarFlags_None)) {

            // ===== CAMERA SETTINGS TAB =====
            if (ImGui::BeginTabItem("Camera Settings")) {
                ImGui::Spacing();
                ImGui::Text("Camera Configuration");
                ImGui::Separator();

                ImGui::Text("Capture Source:");
                int sourceChoice = g_useValveBuiltInCamera ? 1 : 0;
                bool sourceChanged = false;
                sourceChanged |= ImGui::RadioButton("External USB cameras", &sourceChoice, 0);
                ImGui::SameLine();
                sourceChanged |= ImGui::RadioButton("Valve Index built-in", &sourceChoice, 1);

                static std::string s_cameraStatus = g_useValveBuiltInCamera
                    ? "Active: Valve Index built-in"
                    : "Active: External USB cameras";
                if (sourceChanged) {
                    bool wantValve = (sourceChoice == 1);
                    s_cameraStatus = wantValve
                        ? "Switching to Valve built-in (may take ~5s)..."
                        : "Switching to External USB cameras...";

                    bool ok = wantValve
                                ? g_cameras.initializeValveBuiltInCamera()
                                : g_cameras.initialize(config.camera.leftCameraIndex,
                                                       config.camera.rightCameraIndex,
                                                       config.camera.frameWidth,
                                                       config.camera.frameHeight,
                                                       config.camera.fps);
                    if (ok) {
                        g_useValveBuiltInCamera = wantValve;
                        config.camera.useValveBuiltInCamera = wantValve;
                        configChanged = true;
                        s_cameraStatus = wantValve
                            ? "Active: Valve Index built-in"
                            : "Active: External USB cameras";
                        // A camera-source switch means a fresh scene - don't let AI
                        // Segmentation's recurrent state bleed over from the old source.
                        g_segmentationEngine.resetState();
                    } else {
                        std::string err = g_cameras.getLastError();
                        if (err.empty()) err = "(no detail)";
                        s_cameraStatus = std::string("Switch FAILED: ") + err;
                        // Try to restore the previous source.
                        if (g_useValveBuiltInCamera) {
                            g_cameras.initializeValveBuiltInCamera();
                        } else {
                            g_cameras.initialize(config.camera.leftCameraIndex,
                                                 config.camera.rightCameraIndex,
                                                 config.camera.frameWidth,
                                                 config.camera.frameHeight,
                                                 config.camera.fps);
                        }
                    }
                }
                ImGui::TextWrapped("%s", s_cameraStatus.c_str());

                if (!g_useValveBuiltInCamera) {
                    int autoLeft = g_cameras.getLeftAutoSelectedIndex();
                    int autoRight = g_cameras.getRightAutoSelectedIndex();
                    if (autoLeft >= 0) {
                        ImGui::TextWrapped(
                            "Note: configured Left Camera Index (%d) looked wrong or unavailable - "
                            "auto-switched to index %d for this session. Update the slider below and "
                            "save if you want this to stick.",
                            config.camera.leftCameraIndex, autoLeft);
                    }
                    if (autoRight >= 0) {
                        ImGui::TextWrapped(
                            "Note: configured Right Camera Index (%d) looked wrong or unavailable - "
                            "auto-switched to index %d for this session. Update the slider below and "
                            "save if you want this to stick.",
                            config.camera.rightCameraIndex, autoRight);
                    }
                }

                // Live capture diagnostics — useful when something is "not streaming"
                {
                    uint64_t lseq = g_cameras.getLeftSeq();
                    uint64_t rseq = g_cameras.getRightSeq();
                    cv::Mat probeL, probeR;
                    g_cameras.getLatestFrames(probeL, probeR);
                    ImGui::Text("Frames captured  L:%llu  R:%llu",
                                (unsigned long long)lseq, (unsigned long long)rseq);
                    ImGui::Text("Latest size  L:%dx%d  R:%dx%d",
                                probeL.cols, probeL.rows, probeR.cols, probeR.rows);
                    ImGui::Text("Configured size  %dx%d",
                                config.camera.frameWidth, config.camera.frameHeight);
                    if (g_useValveBuiltInCamera) {
                        ImGui::Text("Valve raw combined frame: %ux%u",
                                    g_cameras.getValveRawWidth(), g_cameras.getValveRawHeight());
                        ImGui::Text("Valve per-eye slice (pre-resize, left/right split): %dx%d",
                                    g_cameras.getValveSliceWidth(), g_cameras.getValveSliceHeight());
                    }
                    if (g_useValveBuiltInCamera &&
                        (probeL.cols != config.camera.frameWidth ||
                         probeL.rows != config.camera.frameHeight) &&
                        probeL.cols > 0) {
                        ImGui::TextWrapped(
                            "Note: Valve camera frame size differs from configured size. "
                            "The render pipeline expects %dx%d per eye but cameras deliver %dx%d. "
                            "Preview/overlay may be blank until config is updated.",
                            config.camera.frameWidth, config.camera.frameHeight,
                            probeL.cols, probeL.rows);
                    }
                }
                ImGui::Separator();

                ImGui::Text("Camera Indices (requires restart):");
                KeyboardFriendlySlider("Left Camera Index", &config.camera.leftCameraIndex, 0, 10, "%d");
                KeyboardFriendlySlider("Right Camera Index", &config.camera.rightCameraIndex, 0, 10, "%d");

                ImGui::Spacing();
                if (g_cameras.isCameraScanRunning()) {
                    ImGui::BeginDisabled();
                    ImGui::Button("Scanning...");
                    ImGui::EndDisabled();
                } else if (ImGui::Button("Scan Cameras (indices 0-9)")) {
                    g_cameras.beginCameraScan(9);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Briefly opens each index 0-9 and shows what resolution (if any) "
                                       "it delivers, so you can pick the right Left/Right Camera Index "
                                       "above instead of guessing.");
                }
                {
                    auto scanResults = g_cameras.getCameraScanResults();
                    for (const auto& entry : scanResults) {
                        if (entry.opened) {
                            ImGui::Text("  Index %d: %dx%d", entry.index, entry.width, entry.height);
                        } else {
                            ImGui::Text("  Index %d: not available", entry.index);
                        }
                    }
                }

                ImGui::Spacing();
                ImGui::Text("Resolution & FPS (requires restart):");
                KeyboardFriendlySlider("Frame Width", &config.camera.frameWidth, 320, 1920, "%d");
                KeyboardFriendlySlider("Frame Height", &config.camera.frameHeight, 240, 1080, "%d");
                KeyboardFriendlySlider("FPS", &config.camera.fps, 15, 60, "%d");

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Text("Pixel Offset Adjustment");

                ImGui::Text("Left Camera Offset:");
                configChanged |= DPadWidget("Left Cam", &config.camera.leftPixelOffsetX, &config.camera.leftPixelOffsetY,
                                            -300, 300, -100, 100);

                {
                    // Manual split-fix toggle for the left camera only - it doesn't get the
                    // right camera's auto-detected frame-tear correction, since auto-detect
                    // wasn't reliable for this camera.
                    bool leftSplitFixEnabled = config.camera.splitOffsetPixels != 0;
                    if (ImGui::Checkbox("Left Camera Split Fix (350px)", &leftSplitFixEnabled)) {
                        config.camera.splitOffsetPixels = leftSplitFixEnabled ? 350 : 0;
                        configChanged = true;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Fixes a torn/split left camera frame. Toggle on if the left preview looks cut in half.");
                    }
                }

                ImGui::SameLine(0.0f, 30.0f);

                ImGui::BeginGroup();
                ImGui::Text("Overlay Position:");
                if (DPadWidgetFloat("Overlay XY", &config.overlay.posX, &config.overlay.posY,
                                    -2.0f, 2.0f, -2.0f, 2.0f, 0.001f)) {
                    if (g_pinActive) setPin(config.overlay);
                    g_vrOverlay.resetPosition();
                    configChanged = true;
                }
                ImGui::EndGroup();

                ImGui::Spacing();
                ImGui::Text("Right Camera Offset:");
                configChanged |= KeyboardFriendlySlider("Right Offset X", &config.camera.rightPixelOffsetX, -300, 300, "%d pixels");
                configChanged |= KeyboardFriendlySlider("Right Offset Y", &config.camera.rightPixelOffsetY, -100, 100, "%d pixels");

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Text("Camera Brightness / Contrast");
                ImGui::TextWrapped("Corrects per-eye exposure differences between the two physical "
                                    "cameras. Applied to the raw image before chroma-key/AI segmentation, "
                                    "so it affects both the preview and the headset.");
                ImGui::Text("Left Camera:");
                configChanged |= KeyboardFriendlySlider("Left Brightness", &config.camera.leftBrightness, -100.0f, 100.0f, "%.0f");
                configChanged |= KeyboardFriendlySlider("Left Contrast", &config.camera.leftContrast, 0.5f, 2.0f, "%.2f");
                ImGui::Text("Right Camera:");
                configChanged |= KeyboardFriendlySlider("Right Brightness", &config.camera.rightBrightness, -100.0f, 100.0f, "%.0f");
                configChanged |= KeyboardFriendlySlider("Right Contrast", &config.camera.rightContrast, 0.5f, 2.0f, "%.2f");

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Text("Hand Brightness (Night Mode):");
                configChanged |= KeyboardFriendlySlider("Brightness", &config.overlay.handBrightness, 0.0f, 1.0f, "%.2f");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Dim the hands for night flying. 1.0 = normal, 0.0 = completely dark");
                }

                ImGui::EndTabItem();
            }

            // ===== CHROMA KEY TAB =====
            if (ImGui::BeginTabItem("Chroma Key")) {
                ImGui::Spacing();
                ImGui::Text("Chroma Key Configuration");
                ImGui::Separator();

                ImGui::Text("Keying Mode:");
                {
                    int modeChoice = (g_keyingMode == KeyingMode::AISegmentation) ? 1 : 0;
                    bool modeChanged = false;
                    modeChanged |= ImGui::RadioButton("Chroma Key (legacy)", &modeChoice, 0);
                    ImGui::SameLine();
                    modeChanged |= ImGui::RadioButton("AI Segmentation", &modeChoice, 1);

                    static std::string s_segmentationStatus = "";
                    if (modeChanged) {
                        bool wantAI = (modeChoice == 1);
                        if (wantAI && !g_segmentationEngine.isReady()) {
                            s_segmentationStatus = std::string("AI Segmentation unavailable: ") +
                                                   g_segmentationEngine.getLastError() +
                                                   " - staying on Chroma Key.";
                            g_keyingMode = KeyingMode::ChromaKey;
                        } else {
                            g_keyingMode = wantAI ? KeyingMode::AISegmentation : KeyingMode::ChromaKey;
                            g_segmentationEngine.resetState();
                            config.segmentation.keyingModeAI = wantAI;
                            configChanged = true;
                            s_segmentationStatus = wantAI ? "AI Segmentation: active"
                                                           : "Chroma Key (legacy): active";
                        }
                    }
                    if (!s_segmentationStatus.empty()) {
                        ImGui::TextWrapped("%s", s_segmentationStatus.c_str());
                    }
                }
                ImGui::Spacing();
                ImGui::Separator();

                ImGui::Text("AI Segmentation Tuning");
                configChanged |= ImGui::Checkbox("Dark-background brightness compensation", &config.segmentation.autoGainEnabled);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Lifts dark frames toward the target brightness below before AI "
                                       "segmentation runs (model input only - doesn't change what's "
                                       "displayed). Off by default: try enabling only if hands fade "
                                       "against a dark background, and compare with it off.");
                }
                configChanged |= KeyboardFriendlySlider("Target Brightness", &config.segmentation.autoGainTarget, 60.0f, 220.0f, "%.0f");
                configChanged |= KeyboardFriendlySlider("Max Gain", &config.segmentation.autoGainMaxGain, 1.0f, 5.0f, "%.1f");
                configChanged |= KeyboardFriendlySlider("CLAHE Clip Limit", &config.segmentation.claheClipLimit, 0.5f, 8.0f, "%.1f");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Local contrast enhancement strength on the segmentation model's "
                                       "input. Higher = more edge/detail contrast but more noise amplification.");
                }
                ImGui::Spacing();
                ImGui::Separator();

                // Inverse Mode Toggle
                configChanged |= ImGui::Checkbox("Inverse Mode (Hands Visible, Background Transparent)", &config.chromaKey.inverseMode);
                ImGui::Separator();

                // Border Gradient Mask Settings
                ImGui::Spacing();
                ImGui::Text("Border Gradient Mask (Conceals Black Borders)");
                configChanged |= KeyboardFriendlySlider("Mask Size", &config.chromaKey.borderMaskSize, 0.0f, 0.5f, "%.3f");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Size of gradient fade at edges (0.0 = off, 0.5 = covers half the frame)");
                }
                configChanged |= KeyboardFriendlySlider("Mask Softness", &config.chromaKey.borderMaskSoftness, 0.0f, 0.2f, "%.3f");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Smoothness of the gradient transition");
                }
                ImGui::Separator();

                // Left Eye Chroma Key - use different settings based on mode
                if (ImGui::CollapsingHeader("Left Eye", ImGuiTreeNodeFlags_DefaultOpen)) {
                    float lHue, lRange, lSat, lVal, lEdge;
                    if (config.chromaKey.inverseMode) {
                        lHue = config.chromaKey.leftHueCenterInverse; lRange = config.chromaKey.leftHueRangeInverse;
                        lSat = config.chromaKey.leftSaturationMinInverse; lVal = config.chromaKey.leftValueMinInverse;
                        configChanged |= KeyboardFriendlySlider("Hue Center##Left", &config.chromaKey.leftHueCenterInverse, 0.0f, 360.0f, "%.1f");
                        configChanged |= KeyboardFriendlySlider("Hue Range##Left", &config.chromaKey.leftHueRangeInverse, 0.0f, 180.0f, "%.1f");
                        configChanged |= KeyboardFriendlySlider("Saturation Min##Left", &config.chromaKey.leftSaturationMinInverse, 0.0f, 1.0f, "%.2f");
                        configChanged |= KeyboardFriendlySlider("Value Min##Left", &config.chromaKey.leftValueMinInverse, 0.0f, 1.0f, "%.2f");
                        configChanged |= KeyboardFriendlySlider("Edge Softness##Left", &config.chromaKey.leftEdgeSoftnessInverse, 0.0f, 0.2f, "%.3f");
                    } else {
                        lHue = config.chromaKey.leftHueCenter; lRange = config.chromaKey.leftHueRange;
                        lSat = config.chromaKey.leftSaturationMin; lVal = config.chromaKey.leftValueMin;
                        configChanged |= KeyboardFriendlySlider("Hue Center##Left", &config.chromaKey.leftHueCenter, 0.0f, 360.0f, "%.1f");
                        configChanged |= KeyboardFriendlySlider("Hue Range##Left", &config.chromaKey.leftHueRange, 0.0f, 180.0f, "%.1f");
                        configChanged |= KeyboardFriendlySlider("Saturation Min##Left", &config.chromaKey.leftSaturationMin, 0.0f, 1.0f, "%.2f");
                        configChanged |= KeyboardFriendlySlider("Value Min##Left", &config.chromaKey.leftValueMin, 0.0f, 1.0f, "%.2f");
                        configChanged |= KeyboardFriendlySlider("Edge Softness##Left", &config.chromaKey.leftEdgeSoftness, 0.0f, 0.2f, "%.3f");
                    }

                    // Color preview: show keyed color range
                    {
                        float h = lHue / 360.0f;
                        float hLo = fmodf((lHue - lRange) / 360.0f + 1.0f, 1.0f);
                        float hHi = fmodf((lHue + lRange) / 360.0f, 1.0f);
                        float rC, gC, bC, rLo, gLo, bLo, rHi, gHi, bHi;
                        ImGui::ColorConvertHSVtoRGB(h, 1.0f, 1.0f, rC, gC, bC);
                        ImGui::ColorConvertHSVtoRGB(hLo, 1.0f, 1.0f, rLo, gLo, bLo);
                        ImGui::ColorConvertHSVtoRGB(hHi, 1.0f, 1.0f, rHi, gHi, bHi);
                        float rT, gT, bT;
                        ImGui::ColorConvertHSVtoRGB(h, lSat, lVal, rT, gT, bT);
                        ImGui::Text("Key color:");
                        ImGui::SameLine();
                        ImGui::ColorButton("##lLo", ImVec4(rLo, gLo, bLo, 1), ImGuiColorEditFlags_NoTooltip, ImVec2(20, 20));
                        ImGui::SameLine();
                        ImGui::ColorButton("##lCenter", ImVec4(rC, gC, bC, 1), ImGuiColorEditFlags_NoTooltip, ImVec2(40, 20));
                        ImGui::SameLine();
                        ImGui::ColorButton("##lHi", ImVec4(rHi, gHi, bHi, 1), ImGuiColorEditFlags_NoTooltip, ImVec2(20, 20));
                        ImGui::SameLine();
                        ImGui::Text(" Threshold:");
                        ImGui::SameLine();
                        ImGui::ColorButton("##lThresh", ImVec4(rT, gT, bT, 1), ImGuiColorEditFlags_NoTooltip, ImVec2(20, 20));
                    }

                    // Auto-calibrate button
                    ImGui::Spacing();
                    if (ImGui::Button("Auto-Calibrate Left Eye", ImVec2(200, 0))) {
                        cv::Mat leftFrame, rightFrame;
                        if (g_cameras.getLatestFrames(leftFrame, rightFrame) && !leftFrame.empty()) {
                            autoCalibrateChromaKey(leftFrame, true);
                            configChanged = true;
                        }
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Automatically detect green screen and set optimal chroma key parameters.\nMake sure the green screen fills the center of the frame.");
                    }
                }

                // Right Eye Chroma Key - use different settings based on mode
                if (ImGui::CollapsingHeader("Right Eye", ImGuiTreeNodeFlags_DefaultOpen)) {
                    float rHue, rRange, rSat, rVal, rEdge;
                    if (config.chromaKey.inverseMode) {
                        rHue = config.chromaKey.rightHueCenterInverse; rRange = config.chromaKey.rightHueRangeInverse;
                        rSat = config.chromaKey.rightSaturationMinInverse; rVal = config.chromaKey.rightValueMinInverse;
                        configChanged |= KeyboardFriendlySlider("Hue Center##Right", &config.chromaKey.rightHueCenterInverse, 0.0f, 360.0f, "%.1f");
                        configChanged |= KeyboardFriendlySlider("Hue Range##Right", &config.chromaKey.rightHueRangeInverse, 0.0f, 180.0f, "%.1f");
                        configChanged |= KeyboardFriendlySlider("Saturation Min##Right", &config.chromaKey.rightSaturationMinInverse, 0.0f, 1.0f, "%.2f");
                        configChanged |= KeyboardFriendlySlider("Value Min##Right", &config.chromaKey.rightValueMinInverse, 0.0f, 1.0f, "%.2f");
                        configChanged |= KeyboardFriendlySlider("Edge Softness##Right", &config.chromaKey.rightEdgeSoftnessInverse, 0.0f, 0.2f, "%.3f");
                    } else {
                        rHue = config.chromaKey.rightHueCenter; rRange = config.chromaKey.rightHueRange;
                        rSat = config.chromaKey.rightSaturationMin; rVal = config.chromaKey.rightValueMin;
                        configChanged |= KeyboardFriendlySlider("Hue Center##Right", &config.chromaKey.rightHueCenter, 0.0f, 360.0f, "%.1f");
                        configChanged |= KeyboardFriendlySlider("Hue Range##Right", &config.chromaKey.rightHueRange, 0.0f, 180.0f, "%.1f");
                        configChanged |= KeyboardFriendlySlider("Saturation Min##Right", &config.chromaKey.rightSaturationMin, 0.0f, 1.0f, "%.2f");
                        configChanged |= KeyboardFriendlySlider("Value Min##Right", &config.chromaKey.rightValueMin, 0.0f, 1.0f, "%.2f");
                        configChanged |= KeyboardFriendlySlider("Edge Softness##Right", &config.chromaKey.rightEdgeSoftness, 0.0f, 0.2f, "%.3f");
                    }

                    // Color preview: show keyed color range
                    {
                        float h = rHue / 360.0f;
                        float hLo = fmodf((rHue - rRange) / 360.0f + 1.0f, 1.0f);
                        float hHi = fmodf((rHue + rRange) / 360.0f, 1.0f);
                        float rcC, gcC, bcC, rcLo, gcLo, bcLo, rcHi, gcHi, bcHi;
                        ImGui::ColorConvertHSVtoRGB(h, 1.0f, 1.0f, rcC, gcC, bcC);
                        ImGui::ColorConvertHSVtoRGB(hLo, 1.0f, 1.0f, rcLo, gcLo, bcLo);
                        ImGui::ColorConvertHSVtoRGB(hHi, 1.0f, 1.0f, rcHi, gcHi, bcHi);
                        float rcT, gcT, bcT;
                        ImGui::ColorConvertHSVtoRGB(h, rSat, rVal, rcT, gcT, bcT);
                        ImGui::Text("Key color:");
                        ImGui::SameLine();
                        ImGui::ColorButton("##rLo", ImVec4(rcLo, gcLo, bcLo, 1), ImGuiColorEditFlags_NoTooltip, ImVec2(20, 20));
                        ImGui::SameLine();
                        ImGui::ColorButton("##rCenter", ImVec4(rcC, gcC, bcC, 1), ImGuiColorEditFlags_NoTooltip, ImVec2(40, 20));
                        ImGui::SameLine();
                        ImGui::ColorButton("##rHi", ImVec4(rcHi, gcHi, bcHi, 1), ImGuiColorEditFlags_NoTooltip, ImVec2(20, 20));
                        ImGui::SameLine();
                        ImGui::Text(" Threshold:");
                        ImGui::SameLine();
                        ImGui::ColorButton("##rThresh", ImVec4(rcT, gcT, bcT, 1), ImGuiColorEditFlags_NoTooltip, ImVec2(20, 20));
                    }

                    // Auto-calibrate button
                    ImGui::Spacing();
                    if (ImGui::Button("Auto-Calibrate Right Eye", ImVec2(200, 0))) {
                        cv::Mat leftFrame, rightFrame;
                        if (g_cameras.getLatestFrames(leftFrame, rightFrame) && !rightFrame.empty()) {
                            autoCalibrateChromaKey(rightFrame, false);
                            configChanged = true;
                        }
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Automatically detect green screen and set optimal chroma key parameters.\nMake sure the green screen fills the center of the frame.");
                    }
                }

                ImGui::EndTabItem();
            }

            // ===== OVERLAY POSITION TAB =====
            if (ImGui::BeginTabItem("Overlay Position")) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Locked to Seated Space");
                ImGui::Text("Overlay is fixed in cockpit, independent of head movement");
                ImGui::Separator();

                ImGui::Text("Size:");
                if (KeyboardFriendlySlider("Width", &config.overlay.width, 0.1f, 3.0f, "%.3f meters")) {
                    applyPinCompensation(config.overlay);
                    g_vrOverlay.resetPosition();
                    configChanged = true;
                }
                if (KeyboardFriendlySlider("Height", &config.overlay.height, 0.1f, 3.0f, "%.3f meters")) {
                    applyPinCompensation(config.overlay);
                    g_vrOverlay.resetPosition();
                    configChanged = true;
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Text("Overlay Position (Seated Space):");
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Use D-pad on Camera Settings tab for fine 1mm adjustments");

                if (KeyboardFriendlySlider("X Position (Left/Right)", &config.overlay.posX, -2.0f, 2.0f, "%.3f meters")) {
                    if (g_pinActive) setPin(config.overlay);  // Re-pin at new X
                    g_vrOverlay.resetPosition();
                    configChanged = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Negative = Left, Positive = Right");
                }

                if (KeyboardFriendlySlider("Y Position (Up/Down)", &config.overlay.posY, -2.0f, 2.0f, "%.3f meters")) {
                    if (g_pinActive) setPin(config.overlay);  // Re-pin at new Y
                    g_vrOverlay.resetPosition();
                    configChanged = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Negative = Down, Positive = Up");
                }

                if (KeyboardFriendlySlider("Z Position (Forward/Back)", &config.overlay.posZ, -2.0f, 2.0f, "%.3f meters")) {
                    applyPinCompensation(config.overlay);  // Auto-adjust X/Y
                    g_vrOverlay.resetPosition();
                    configChanged = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Negative = Back, Positive = Forward");
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Text("Overlay Rotation:");

                if (KeyboardFriendlySlider("Pitch (Tilt Up/Down)", &config.overlay.pitch, -180.0f, 180.0f, "%.1f degrees")) {
                    applyPinCompensation(config.overlay);
                    g_vrOverlay.resetPosition();
                    configChanged = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Positive = Tilt top edge back");
                }

                if (KeyboardFriendlySlider("Yaw (Turn Left/Right)", &config.overlay.yaw, -180.0f, 180.0f, "%.1f degrees")) {
                    applyPinCompensation(config.overlay);
                    g_vrOverlay.resetPosition();
                    configChanged = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Positive = Turn right");
                }

                if (KeyboardFriendlySlider("Roll (Tilt Sideways)", &config.overlay.roll, -180.0f, 180.0f, "%.1f degrees")) {
                    applyPinCompensation(config.overlay);
                    g_vrOverlay.resetPosition();
                    configChanged = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Positive = Tilt right side down");
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Text("Pin-Point Calibration:");
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                    "Pin a point on the overlay, then adjust Z/Size freely");

                ImGui::Checkbox("Show Crosshair", &g_showCrosshair);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Show red crosshair on overlay at pin position");
                }

                if (KeyboardFriendlySlider("Pin X (Left-Right)", &g_pinU, 0.0f, 1.0f, "%.2f")) {
                    if (g_pinActive) setPin(config.overlay);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("0.0 = Left edge, 0.5 = Center, 1.0 = Right edge");
                }
                if (KeyboardFriendlySlider("Pin Y (Top-Bottom)", &g_pinV, 0.0f, 1.0f, "%.2f")) {
                    if (g_pinActive) setPin(config.overlay);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("0.0 = Top edge, 0.5 = Center, 1.0 = Bottom edge");
                }

                if (!g_pinActive) {
                    if (ImGui::Button("Set Pin", ImVec2(120, 0))) {
                        setPin(config.overlay);
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Lock this point - Z/Size changes will keep it aligned");
                    }
                } else {
                    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "PIN ACTIVE");
                    ImGui::SameLine();
                    if (ImGui::Button("Clear Pin", ImVec2(120, 0))) {
                        g_pinActive = false;
                    }
                }

                ImGui::Spacing();
                ImGui::Separator();
                if (ImGui::Button("Reset to Default Position", ImVec2(250, 0))) {
                    g_pinActive = false;
                    config.overlay.posX = 0.036f;
                    config.overlay.posY = -0.285f;
                    config.overlay.posZ = -0.457f;
                    config.overlay.pitch = 0.0f;
                    config.overlay.yaw = -1.5f;
                    config.overlay.roll = -16.3f;
                    g_vrOverlay.resetPosition();
                    configChanged = true;
                }

                ImGui::EndTabItem();
            }

            // ===== DIAGNOSTICS TAB =====
            if (ImGui::BeginTabItem("Diagnostics")) {
                ImGui::Spacing();
                ImGui::Text("Performance Statistics");
                ImGui::Separator();

                ImGui::Text("Process Time: %.2f ms", g_perfStats.avgProcessTimeMs.load(std::memory_order_relaxed));
                ImGui::Text("Capture Frames: %llu", g_perfStats.captureFrameCount.load(std::memory_order_relaxed));
                ImGui::Text("Render Frames: %llu", g_perfStats.renderFrameCount.load(std::memory_order_relaxed));
                if (g_keyingMode == KeyingMode::AISegmentation) {
                    ImGui::Text("AI Segmentation Time (both eyes): %.2f ms",
                                g_perfStats.avgSegmentationTimeMs.load(std::memory_order_relaxed));
                    bool onGpuNow = g_segmentationEngine.isGpuReady() && !isFlightSimulatorForeground();
                    ImGui::Text("Segmentation running on: %s%s", onGpuNow ? "GPU" : "CPU",
                                g_segmentationEngine.isGpuReady() ? "" : " (GPU unavailable)");
                }

                ImGui::EndTabItem();
            }

            // ===== PROFILES TAB =====
            if (ImGui::BeginTabItem("Profiles")) {
                ImGui::Spacing();
                ImGui::Text("Profile Management");
                ImGui::Separator();

                // Get list of profiles
                std::vector<std::string> profiles = config.listProfiles();
                std::string currentProfile = config.getCurrentProfileName();

                // Current profile display
                ImGui::Text("Current Profile: %s", currentProfile.c_str());
                ImGui::Spacing();
                ImGui::Separator();

                // Profile selector
                static int selectedProfileIndex = 0;
                static char profileNameBuffer[128] = "";

                // Ensure selectedProfileIndex is within bounds
                if (selectedProfileIndex >= static_cast<int>(profiles.size())) {
                    selectedProfileIndex = 0;
                }

                if (profiles.size() > 0) {
                    ImGui::Text("Load Existing Profile:");

                    // Dropdown to select profile
                    if (ImGui::BeginCombo("Select Profile", profiles[selectedProfileIndex].c_str())) {
                        for (size_t i = 0; i < profiles.size(); i++) {
                            bool isSelected = (selectedProfileIndex == static_cast<int>(i));
                            if (ImGui::Selectable(profiles[i].c_str(), isSelected)) {
                                selectedProfileIndex = static_cast<int>(i);
                            }
                            if (isSelected) {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }

                    ImGui::SameLine();

                    // Load button
                    if (ImGui::Button("Load Profile")) {
                        if (config.loadProfile(profiles[selectedProfileIndex])) {
                            std::cout << "Loaded profile: " << profiles[selectedProfileIndex] << std::endl;
                            std::cout << "  ChromaKey Left: Hue=" << config.chromaKey.leftHueCenter
                                      << " Range=" << config.chromaKey.leftHueRange
                                      << " SatMin=" << config.chromaKey.leftSaturationMin
                                      << " ValMin=" << config.chromaKey.leftValueMin << std::endl;
                            std::cout << "  ChromaKey Right: Hue=" << config.chromaKey.rightHueCenter
                                      << " Range=" << config.chromaKey.rightHueRange
                                      << " SatMin=" << config.chromaKey.rightSaturationMin
                                      << " ValMin=" << config.chromaKey.rightValueMin << std::endl;
                            std::cout << "  InverseMode=" << config.chromaKey.inverseMode << std::endl;
                            // Apply overlay settings after loading
                            g_vrOverlay.setOverlayTransform(config.overlay.width, config.overlay.height,
                                config.overlay.distance, config.overlay.horizontalOffset,
                                config.overlay.verticalOffset);
                            g_vrOverlay.setOpacity(config.overlay.opacity);
                            g_vrOverlay.resetPosition();  // Apply rotation + chroma key settings
                        } else {
                            std::cerr << "Failed to load profile: " << profiles[selectedProfileIndex] << std::endl;
                        }
                    }
                } else {
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No saved profiles found.");
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Text("Save Current Settings as New Profile:");

                // Text input for new profile name
                ImGui::InputText("Profile Name", profileNameBuffer, sizeof(profileNameBuffer));
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Enter a name for the new profile (special characters will be replaced)");
                }

                ImGui::SameLine();

                // Save profile button
                if (ImGui::Button("Save Profile", ImVec2(120, 0))) {
                    std::string profileName(profileNameBuffer);
                    if (!profileName.empty()) {
                        if (config.saveProfile(profileName)) {
                            std::cout << "Saved profile: " << profileName << std::endl;
                            profileNameBuffer[0] = '\0';  // Clear input
                        } else {
                            std::cerr << "Failed to save profile: " << profileName << std::endl;
                        }
                    } else {
                        std::cout << "Please enter a profile name" << std::endl;
                    }
                }

                // Delete profile button
                if (profiles.size() > 0) {
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Text("Delete Profile:");

                    if (ImGui::Button("Delete Selected Profile", ImVec2(200, 0))) {
                        if (config.deleteProfile(profiles[selectedProfileIndex])) {
                            std::cout << "Deleted profile: " << profiles[selectedProfileIndex] << std::endl;
                            selectedProfileIndex = 0;  // Reset selection
                        } else {
                            std::cerr << "Failed to delete profile: " << profiles[selectedProfileIndex] << std::endl;
                        }
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Cannot delete the currently loaded profile");
                    }
                }

                ImGui::EndTabItem();
            }

            // ===== TOUCH CALIBRATION TAB =====
            // Capacitive touch is not built/wired up yet (see TOUCH_CALIBRATION.md and
            // arduino/CapacitiveTouchZones/) - this tab is fully functional against whatever
            // is plugged into the configured COM port once it exists. Scope for now is
            // logging/visualizing the matched button, not driving SimConnect yet.
            if (ImGui::BeginTabItem("Touch Calibration")) {
                ImGui::Spacing();
                ImGui::TextWrapped(
                    "Matches a capacitive-touch event to a calibrated virtual button using the "
                    "fingertip position found in the AI segmentation mask at the moment of "
                    "contact. Requires AI Segmentation mode (Chroma Key tab) to be active.");
                ImGui::Separator();

                ImGui::Checkbox("Enable touch matching", &config.touch.enabled);
                ImGui::TextWrapped(
                    "The touch microcontroller presents itself as a USB HID gamepad (one "
                    "button per touch zone) - no COM port. Pick it below like any joystick.");

                // List currently-present joysticks so the right one can be picked without
                // guessing IDs - same enumeration GLFW/main.cpp already does at startup for
                // the reset-button joystick.
                ImGui::Text("Detected joysticks:");
                bool anyJoystick = false;
                for (int i = GLFW_JOYSTICK_1; i <= GLFW_JOYSTICK_LAST; i++) {
                    if (glfwJoystickPresent(i)) {
                        anyJoystick = true;
                        const char* name = glfwGetJoystickName(i);
                        ImGui::BulletText("ID %d: %s", i, name ? name : "(unnamed)");
                    }
                }
                if (!anyJoystick) {
                    ImGui::TextDisabled("(none detected)");
                }

                ImGui::SetNextItemWidth(120);
                ImGui::InputInt("Joystick ID (-1 = auto)", &config.touch.joystickID);

                if (g_touchInput.isConnected()) {
                    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Connected (joystick ID %d)",
                                        g_touchInput.getJoystickID());
                    ImGui::SameLine();
                    if (ImGui::Button("Disconnect")) {
                        g_touchInput.disconnect();
                    }
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Not connected");
                    if (!g_touchInput.getLastError().empty()) {
                        ImGui::TextWrapped("Last error: %s", g_touchInput.getLastError().c_str());
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Connect")) {
                        if (!g_touchInput.connect(config.touch.joystickID)) {
                            std::cerr << "[Touch] Connect failed: " << g_touchInput.getLastError() << std::endl;
                        }
                    }
                }

                ImGui::Spacing();
                ImGui::Separator();
                const char* edgeNames[] = { "Bottom", "Top", "Left", "Right" };
                ImGui::SetNextItemWidth(150);
                ImGui::Combo("Arm entry edge", &config.touch.entryEdge, edgeNames, 4);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Which side of the camera frame the arm enters from - "
                                       "used to tell the fingertip apart from the wrist.");
                }
                ImGui::SetNextItemWidth(150);
                ImGui::SliderFloat("Max match distance", &config.touch.matchMaxDistNorm, 0.01f, 0.5f, "%.3f");
                ImGui::SetNextItemWidth(150);
                ImGui::SliderFloat("Dial angle smoothing", &config.touch.axisFilterAlpha, 0.05f, 1.0f, "%.2f");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("1.0 = no smoothing. Lower = smoother but more lag - "
                                       "affects both the 'axis' readout below and real dial "
                                       "rotation tracking. Tune empirically against real footage.");
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Text("Live hand diagnostics (updates every frame, AI Segmentation only):");
                if (g_keyingMode != KeyingMode::AISegmentation) {
                    ImGui::TextDisabled("Switch to AI Segmentation mode (Chroma Key tab) to see these.");
                } else {
                    for (int eyeIdx = 0; eyeIdx < 2; ++eyeIdx) {
                        bool isLeft = (eyeIdx == 0);
                        const cv::Point2f& fingertip = isLeft ? g_liveHandDiag.fingertipLeft : g_liveHandDiag.fingertipRight;
                        float orientation = isLeft ? g_liveHandDiag.orientationLeftDeg : g_liveHandDiag.orientationRightDeg;
                        float direction = isLeft ? g_liveHandDiag.directionLeftDeg : g_liveHandDiag.directionRightDeg;

                        ImGui::Text("%s eye:", isLeft ? "Left" : "Right");
                        ImGui::SameLine();
                        if (fingertip.x >= 0.0f) {
                            ImGui::Text("pos (%.3f, %.3f)", fingertip.x, fingertip.y);
                        } else {
                            ImGui::TextDisabled("pos: no hand detected");
                        }
                        ImGui::SameLine();
                        if (!std::isnan(orientation)) {
                            ImGui::Text("| axis %.1f deg (0-180)", orientation);
                        } else {
                            ImGui::TextDisabled("| axis: n/a");
                        }
                        ImGui::SameLine();
                        if (!std::isnan(direction)) {
                            ImGui::Text("| twist %.1f deg (+-180)", direction);
                        } else {
                            ImGui::TextDisabled("| twist: n/a");
                        }
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "pos: fingertip, normalized 0-1 (0,0 = top-left).\n"
                            "axis: hand silhouette's orientation - undirected, so 10 and 190 "
                            "degrees look identical (this is what drives dial tracking).\n"
                            "twist: centroid->fingertip direction - a true +-180 reading with "
                            "no ambiguity, easier to watch sweep as you turn your wrist.");
                    }
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Text("Last touch event:");
                bool haveRecent = g_lastTouchMatchTime.time_since_epoch().count() != 0 &&
                    std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::steady_clock::now() - g_lastTouchMatchTime).count() < 3;
                if (haveRecent) {
                    if (g_lastTouchMatch.matched) {
                        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Matched: %s (%s eye, %.3f, %.3f)",
                                            g_lastTouchMatch.buttonName.c_str(),
                                            g_lastTouchMatch.isLeftEye ? "left" : "right",
                                            g_lastTouchMatch.fingertipXNorm, g_lastTouchMatch.fingertipYNorm);
                    } else {
                        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "No match (%s eye, %.3f, %.3f)",
                                            g_lastTouchMatch.isLeftEye ? "left" : "right",
                                            g_lastTouchMatch.fingertipXNorm, g_lastTouchMatch.fingertipYNorm);
                    }
                } else {
                    ImGui::TextDisabled("(none in the last 3 seconds)");
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Text("Active dial:");
                if (g_activeDialDrag.active) {
                    const auto& dialBtn = config.touch.buttons[g_activeDialDrag.buttonIndex];
                    ImGui::SameLine();
                    ImGui::Text("%s", dialBtn.name.c_str());

                    ImGui::SetWindowFontScale(1.8f);
                    ImGui::Text("%+d", g_activeDialDrag.tickCount);
                    ImGui::SetWindowFontScale(1.0f);

                    // A bidirectional bar centered on zero (ImGui::ProgressBar is 0-1 only
                    // and doesn't fit a value that can go negative) - fills right for
                    // positive ticks, left for negative, so the count can be read as a
                    // glance rather than needing to focus on small text. Meant for this to
                    // be easy to check on a monitor without a headset on while tuning;
                    // whether it also needs to be visible inside the headset itself (a
                    // separate, bigger change to the injected VR overlay layer) is still
                    // open - see TOUCH_CALIBRATION.md.
                    constexpr int kBarRangeTicks = 20;  // ticks shown as full deflection each way
                    float frac = std::clamp(static_cast<float>(g_activeDialDrag.tickCount)
                                             / static_cast<float>(kBarRangeTicks), -1.0f, 1.0f);

                    ImVec2 barSize(300.0f, 24.0f);
                    ImVec2 barPos = ImGui::GetCursorScreenPos();
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    drawList->AddRectFilled(barPos, ImVec2(barPos.x + barSize.x, barPos.y + barSize.y),
                                             ImGui::GetColorU32(ImGuiCol_FrameBg));
                    float centerX = barPos.x + barSize.x * 0.5f;
                    float fillX = centerX + frac * (barSize.x * 0.5f);
                    ImU32 fillColor = (frac >= 0.0f) ? IM_COL32(80, 200, 80, 255) : IM_COL32(220, 80, 80, 255);
                    float fillLeft = (centerX < fillX) ? centerX : fillX;
                    float fillRight = (centerX < fillX) ? fillX : centerX;
                    drawList->AddRectFilled(ImVec2(fillLeft, barPos.y),
                                             ImVec2(fillRight, barPos.y + barSize.y), fillColor);
                    drawList->AddLine(ImVec2(centerX, barPos.y), ImVec2(centerX, barPos.y + barSize.y),
                                       IM_COL32(255, 255, 255, 180), 2.0f);
                    drawList->AddRect(barPos, ImVec2(barPos.x + barSize.x, barPos.y + barSize.y),
                                       IM_COL32(200, 200, 200, 255));
                    ImGui::Dummy(barSize);  // reserve layout space for the manually-drawn bar
                } else {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(none)");
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Text("Calibrated buttons (%d):", static_cast<int>(config.touch.buttons.size()));

                if (g_touchCalibrationArmed) {
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f),
                        "Armed - touch the real button on the panel now...");
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel##calib")) {
                        g_touchCalibrationArmed = false;
                        g_touchCalibrationTargetIndex = -1;
                    }
                }

                int deleteIndex = -1;
                for (int i = 0; i < static_cast<int>(config.touch.buttons.size()); ++i) {
                    auto& btn = config.touch.buttons[i];
                    ImGui::PushID(i);
                    ImGui::Text("%-20s zone=%-3d %s %s (%.3f, %.3f)", btn.name.c_str(), btn.zone,
                                btn.type == TouchControlType::Dial ? "Dial" : "Btn ",
                                btn.isLeftEye ? "L" : "R", btn.xNorm, btn.yNorm);
                    ImGui::SameLine();
                    if (ImGui::SmallButton(btn.type == TouchControlType::Dial ? "Type:Dial" : "Type:Btn")) {
                        btn.type = (btn.type == TouchControlType::Dial) ? TouchControlType::Button
                                                                         : TouchControlType::Dial;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Dial: rotation is tracked by hand twist while held, "
                                           "not just a single touch position.");
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton(btn.isLeftEye ? "Eye:L" : "Eye:R")) {
                        btn.isLeftEye = !btn.isLeftEye;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Capture")) {
                        g_touchCalibrationArmed = true;
                        g_touchCalibrationTargetIndex = i;
                        g_touchCalibrationTargetEye = btn.isLeftEye;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Delete")) {
                        deleteIndex = i;
                    }
                    ImGui::PopID();
                }
                if (deleteIndex >= 0) {
                    config.touch.buttons.erase(config.touch.buttons.begin() + deleteIndex);
                    if (g_touchCalibrationTargetIndex == deleteIndex) {
                        g_touchCalibrationArmed = false;
                        g_touchCalibrationTargetIndex = -1;
                    }
                }

                ImGui::Spacing();
                static char newButtonName[64] = "";
                ImGui::SetNextItemWidth(200);
                ImGui::InputText("##newButtonName", newButtonName, sizeof(newButtonName));
                ImGui::SameLine();
                if (ImGui::Button("Add New Button") && newButtonName[0] != '\0') {
                    TouchButtonCalibration newBtn;
                    newBtn.name = newButtonName;
                    config.touch.buttons.push_back(newBtn);
                    newButtonName[0] = '\0';
                }

                ImGui::Spacing();
                ImGui::TextDisabled("Use \"Save to Config File\" below to persist calibration.");

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Text("Testing without hardware:");
                ImGui::TextWrapped(
                    "Simulates a touch on a zone so the whole pipeline - calibration capture, "
                    "matching, and (if held) dial rotation - can be exercised against your "
                    "real hand and real camera footage, without any touch device plugged in.");

                static int simulateZone = 0;
                ImGui::SetNextItemWidth(100);
                ImGui::InputInt("Zone##simulate", &simulateZone);
                if (simulateZone < 0) simulateZone = 0;

                static bool simulateHeld = false;
                if (!simulateHeld) {
                    if (ImGui::Button("Simulate Touch (press)")) {
                        g_touchInput.injectSimulatedPress(simulateZone);
                        simulateHeld = true;
                    }
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f),
                        "Zone %d simulated-held - move/twist your hand in view of the camera now",
                        simulateZone);
                    ImGui::SameLine();
                    if (ImGui::Button("Release##simulate")) {
                        g_touchInput.injectSimulatedRelease(simulateZone);
                        simulateHeld = false;
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Only meaningful while not connected to a real device - "
                                       "a real gamepad's own state would immediately override it.");
                }

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::Spacing();
        ImGui::Separator();

        // Save and Load Default buttons (outside tabs, at bottom of window)
        if (ImGui::Button("Save to Config File", ImVec2(240, 0))) {
            std::cout << "\n=== Saving Config ===" << std::endl;
            if (config.save("config/settings.ini")) {
                std::cout << "Configuration saved successfully" << std::endl;
            } else {
                std::cerr << "Failed to save configuration" << std::endl;
            }
        }

        ImGui::SameLine();

        if (ImGui::Button("Load Default Settings", ImVec2(240, 0))) {
            // Reset to hardcoded defaults from Config.h
            config.camera = CameraConfig();
            config.chromaKey = ChromaKeyConfig();
            config.overlay = OverlayConfig();
            config.input = InputConfig();
            config.segmentation = SegmentationConfig();
            g_keyingMode = KeyingMode::ChromaKey;

            // Apply the default overlay settings
            g_vrOverlay.setOverlayTransform(config.overlay.width,
                                            config.overlay.height,
                                            config.overlay.distance,
                                            config.overlay.horizontalOffset,
                                            config.overlay.verticalOffset);
            g_vrOverlay.setOpacity(config.overlay.opacity);
            g_vrOverlay.resetPosition();

            std::cout << "\n=== Loaded default settings ===" << std::endl;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Reset all settings to hardcoded defaults from Config.h");
        }

        ImGui::End();

        // Render ImGui and swap buffers ONLY if window is valid
        if (!isIconified && windowValid) {
            // Render ImGui
            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

            glfwSwapBuffers(g_window);
        } else {
            // Window is iconified or invalid - skip rendering but still process ImGui frame
            // This keeps ImGui state valid even when window is minimized
            ImGui::Render();
            // Note: No additional sleep needed - frame rate limiter handles this
        }

        // Auto-save: debounced 2-second delay after last change
        if (configChanged) {
            lastConfigChangeTime = std::chrono::steady_clock::now();
            configDirty = true;
            configChanged = false;
        }
        if (configDirty) {
            auto sinceLast = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - lastConfigChangeTime).count();
            if (sinceLast >= 2000) {
                if (config.save("config/settings.ini")) {
                    std::cout << "[AutoSave] Settings saved" << std::endl;
                }
                configDirty = false;
            }
        }

        // Print performance stats every 10 seconds (reduced frequency to minimize I/O overhead)
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - lastStatsTime).count() >= 10) {
            g_perfStats.updateDroppedFrames();
            g_perfStats.printStats();

            // Add focus state to performance output
            if (!hasFocus) {
                std::cout << "  [Focus: MSFS/other app - VR feed should still be active]" << std::endl;
            } else {
                std::cout << "  [Focus: Overlay window]" << std::endl;
            }

            g_perfStats.reset();
            lastStatsTime = now;
        }

        // Display head movement tracking every 500ms
        frameCount++;
        auto currentTime = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastFrameTime).count();

        if (elapsed >= 500) {
            float yawDelta = g_vrOverlay.getYawDelta();
            float compensationYaw = g_vrOverlay.getCompensationYaw();
            float pitchDelta = g_vrOverlay.getPitchDelta();
            float compensationPitch = g_vrOverlay.getCompensationPitch();

            std::cout << "Head Tracking - Yaw Δ: " << (yawDelta * 180.0f / 3.14159265f)
                      << "° (" << compensationYaw << "m) | Pitch Δ: " << (pitchDelta * 180.0f / 3.14159265f)
                      << "° (" << compensationPitch << "m)" << std::endl;

            frameCount = 0;
            lastFrameTime = currentTime;
        }

        // No sleep - let main thread run at maximum speed to consume frames ASAP
        // Camera threads control frame rate naturally at 30 FPS
    }
}

void cleanup() noexcept {
    std::cout << "Cleaning up..." << std::endl;

    // Cleanup shaders
    g_chromaKeyShader.cleanup();
    g_previewShader.cleanup();

    if (g_vao != 0) glDeleteVertexArrays(1, &g_vao);
    if (g_vbo != 0) glDeleteBuffers(1, &g_vbo);
    if (g_framebuffer != 0) glDeleteFramebuffers(1, &g_framebuffer);
    if (g_stereoTexture != 0) glDeleteTextures(1, &g_stereoTexture);
    if (g_leftCameraTexture != 0) glDeleteTextures(1, &g_leftCameraTexture);
    if (g_rightCameraTexture != 0) glDeleteTextures(1, &g_rightCameraTexture);
    if (g_leftPBO != 0) glDeleteBuffers(1, &g_leftPBO);
    if (g_rightPBO != 0) glDeleteBuffers(1, &g_rightPBO);
    if (g_readbackPBOs[0] != 0) glDeleteBuffers(2, g_readbackPBOs);

    g_cameras.release();
    g_touchInput.disconnect();
    g_vrOverlay.shutdown();

    // Cleanup ImGui
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if (g_window) {
        glfwDestroyWindow(g_window);
        glfwTerminate();
    }
}

int main(int argc, char* argv[]) {
    // Redirect stdout/stderr to log file (WIN32 app has no console)
    static std::ofstream logFile("C:\\Temp\\MSFSHandOverlay_App.log", std::ios::app);
    if (logFile.is_open()) {
        logFile << "\n\n=== NEW SESSION " << __DATE__ << " " << __TIME__ << " ===\n";
        logFile.flush();
        std::cout.rdbuf(logFile.rdbuf());
        std::cerr.rdbuf(logFile.rdbuf());
    }

    // Set working directory to exe location (so shaders/ and config/ are found)
    {
        char exePath[MAX_PATH];
        GetModuleFileNameA(NULL, exePath, MAX_PATH);
        std::string exeDir(exePath);
        size_t lastSlash = exeDir.find_last_of("\\/");
        if (lastSlash != std::string::npos) {
            exeDir = exeDir.substr(0, lastSlash);
            SetCurrentDirectoryA(exeDir.c_str());
        }
    }

    std::cout << "=== MSFS Hand Overlay ===" << std::endl;
    std::cout << "Version 1.0" << std::endl << std::endl;

    // Set process priority to NORMAL (minimal impact on MSFS)
    // Lower priority = less CPU competition with MSFS
    HANDLE hProcess = GetCurrentProcess();
    if (SetPriorityClass(hProcess, NORMAL_PRIORITY_CLASS)) {
        std::cout << "[Init] Process priority set to NORMAL - minimal MSFS impact" << std::endl;
    } else {
        std::cerr << "[Init] Warning: Failed to set NORMAL priority" << std::endl;
    }

    // Load configuration
    Config& config = Config::getInstance();
    std::string configPath = "config/settings.ini";

    if (!config.load(configPath)) {
        std::cout << "Config file not found, creating default..." << std::endl;
        config.save(configPath);
    } else {
        std::cout << "Config loaded successfully from " << configPath << std::endl;
    }

    // Create profiles directory if it doesn't exist
    CreateDirectoryA("config\\profiles", NULL);

    // Save default settings as profile "1" (overwrites any existing profile "1")
    {
        // Temporarily swap in defaults, save as profile, then restore current config
        CameraConfig savedCam = config.camera;
        ChromaKeyConfig savedChroma = config.chromaKey;
        OverlayConfig savedOverlay = config.overlay;
        InputConfig savedInput = config.input;

        config.camera = CameraConfig();
        config.chromaKey = ChromaKeyConfig();
        config.overlay = OverlayConfig();
        config.input = InputConfig();
        config.saveProfile("1");
        std::cout << "Default settings saved as profile '1'" << std::endl;

        // Restore the user's actual loaded config
        config.camera = savedCam;
        config.chromaKey = savedChroma;
        config.overlay = savedOverlay;
        config.input = savedInput;
    }

    // Initialize OpenGL
    std::cout << "[Init] Initializing OpenGL..." << std::endl;
    if (!initializeOpenGL()) {
        std::cerr << "Failed to initialize OpenGL" << std::endl;
        return 1;
    }
    std::cout << "[Init] OpenGL initialization complete" << std::endl;

    // Initialize DirectX 11 (for VR overlay submission)
    std::cout << "[Init] Initializing DirectX 11..." << std::endl;
    try {
        if (!initializeDirectX()) {
            std::cerr << "Failed to initialize DirectX 11" << std::endl;
            cleanup();
            return 1;
        }
        std::cout << "[Init] DirectX 11 initialization complete" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[Init] Exception during DirectX init: " << e.what() << std::endl;
        cleanup();
        return 1;
    } catch (...) {
        std::cerr << "[Init] Unknown exception during DirectX init" << std::endl;
        cleanup();
        return 1;
    }

    // Initialize shared memory for API layer (non-fatal)
    std::cout << "[Init] Initializing shared memory for API layer..." << std::endl;
    if (!g_vrOverlay.initialize("MSFS Hand Overlay")) {
        std::cerr << "WARNING: Failed to initialize shared memory - VR overlay will not work" << std::endl;
        std::cerr << "  (This is OK if you're just testing the preview window)" << std::endl;
    } else {
        std::cout << "[Init] Shared memory initialized - API layer ready" << std::endl;
    }

    // Initialize cameras, restoring whichever capture source was active last session
    // (g_useValveBuiltInCamera used to be a bare global that reset to "external USB" on
    // every launch regardless of what was configured - now it follows the persisted choice).
    bool camerasReady = false;
    if (config.camera.useValveBuiltInCamera) {
        camerasReady = g_cameras.initializeValveBuiltInCamera();
        if (camerasReady) {
            g_useValveBuiltInCamera = true;
        } else {
            std::cerr << "[Init] Valve built-in camera failed (" << g_cameras.getLastError()
                       << "), falling back to external USB cameras" << std::endl;
        }
    }
    if (!camerasReady) {
        camerasReady = g_cameras.initialize(config.camera.leftCameraIndex,
                                             config.camera.rightCameraIndex,
                                             config.camera.frameWidth,
                                             config.camera.frameHeight,
                                             config.camera.fps);
    }
    if (!camerasReady) {
        std::cerr << "Failed to initialize cameras" << std::endl;
        cleanup();
        return 1;
    }

    // Attempt to load the AI segmentation model unconditionally at startup (cheap enough
    // to always try, so switching to AI mode later doesn't hit a cold-start stall). This
    // does NOT change the active keying mode - default stays Chroma Key regardless of
    // whether this succeeds, so behavior never changes unless the user opts in via the UI.
    if (g_segmentationEngine.loadModel("models/rvm_mobilenetv3_fp32.onnx")) {
        std::cout << "[Init] AI Segmentation model loaded and ready" << std::endl;
    } else {
        std::cout << "[Init] AI Segmentation unavailable: " << g_segmentationEngine.getLastError()
                  << " (Chroma Key mode still works normally)" << std::endl;
    }

    // Restore whichever keying mode was active last session. g_keyingMode itself isn't part
    // of Config (it's a plain global), so without this it silently reset to Chroma Key on
    // every launch regardless of what the user had selected.
    if (config.segmentation.keyingModeAI && g_segmentationEngine.isReady()) {
        g_keyingMode = KeyingMode::AISegmentation;
        g_segmentationEngine.resetState();
    }

    // Connect to the capacitive touch panel's microcontroller, if configured. It presents
    // as a USB HID gamepad (one button per touch zone - see arduino/CapacitiveTouchZones_ESP32/),
    // so "connecting" is just finding it among GLFW's enumerated joysticks. Non-fatal on
    // failure (e.g. the hardware isn't built/plugged in yet) - the app works normally
    // without it, same philosophy as the GPU segmentation session being best-effort. The
    // Touch Calibration tab's Connect button can retry later.
    if (config.touch.enabled) {
        if (g_touchInput.connect(config.touch.joystickID)) {
            std::cout << "[Init] Touch input connected (joystick ID " << g_touchInput.getJoystickID()
                       << ")" << std::endl;
        } else {
            std::cerr << "[Init] Touch input unavailable: " << g_touchInput.getLastError() << std::endl;
        }
    }

    // Load chroma key shader
    if (!g_chromaKeyShader.loadFromFiles("shaders/vertex.glsl", "shaders/chromakey_fragment.glsl")) {
        std::cerr << "Failed to load shaders" << std::endl;
        cleanup();
        return 1;
    }

    // Load preview shader
    if (!g_previewShader.loadFromFiles("shaders/vertex.glsl", "shaders/preview_fragment.glsl")) {
        std::cerr << "Failed to load preview shader" << std::endl;
        cleanup();
        return 1;
    }

    // Create OpenGL resources
    if (!createStereoTexture(config.camera.frameWidth, config.camera.frameHeight)) {
        std::cerr << "Failed to create stereo texture" << std::endl;
        cleanup();
        return 1;
    }

    // Initialize WGL interop for zero-copy GPU transfer (eliminates CPU roundtrip)
    if (!initializeWGLInterop()) {
        std::cerr << "[WARNING] WGL interop failed - using CPU roundtrip (may cause stalls)" << std::endl;
        // Non-fatal: will fall back to old method
    }

    if (!createQuadGeometry()) {
        std::cerr << "Failed to create quad geometry" << std::endl;
        cleanup();
        return 1;
    }

    // Configure overlay transform
    g_vrOverlay.setOverlayTransform(config.overlay.width,
                                    config.overlay.height,
                                    config.overlay.distance,
                                    config.overlay.horizontalOffset,
                                    config.overlay.verticalOffset);
    g_vrOverlay.setOpacity(config.overlay.opacity);

    // Show overlay
    if (!g_vrOverlay.show()) {
        std::cerr << "Failed to show overlay" << std::endl;
        cleanup();
        return 1;
    }

    // Capture initial head position for dynamic compensation (always enabled)
    g_vrOverlay.resetPosition();
    std::cout << "Dynamic compensation active - initial position captured" << std::endl;

    std::cout << "\n=== Overlay is now active ===" << std::endl;
    std::cout << "=== Multi-Threaded Architecture ===" << std::endl;
    std::cout << "Capture Thread: Running at camera max speed (~30 FPS)" << std::endl;
    std::cout << "Main Thread: Consuming from lock-free triple buffer" << std::endl;
    std::cout << "Expected Latency: ~1ms (memory copy time)" << std::endl;
    std::cout << "\n=== Performance Optimizations (AGGRESSIVE MODE) ===" << std::endl;
    std::cout << "Process Priority: NORMAL (minimal CPU competition)" << std::endl;
    std::cout << "Main Loop: 20 FPS cap (low frame rate for minimal impact)" << std::endl;
    std::cout << "VSync: DISABLED (no GPU sync wait)" << std::endl;
    std::cout << "Window: Normal mode (no always-on-top overhead)" << std::endl;
    std::cout << "\n=== Background Operation ===" << std::endl;
    std::cout << "Video feed continues when MSFS is in focus" << std::endl;
    std::cout << "Minimized rendering: Skipped (GPU resources freed for MSFS)" << std::endl;
    std::cout << "Press ESC or close window to exit" << std::endl << std::endl;

    // Per-camera threads are already running (launched by CameraCapture::initialize)
    std::cout << "[Main] Camera threads running (one per camera with grab() backlog dumping)" << std::endl;

    // No tracking thread needed - overlay is locked to Seated space (cockpit)
    // TrackingUniverseSeated keeps overlay fixed to cockpit, survives MSFS view resets
    // Launch tracking thread
    // g_trackingRunning.store(true, std::memory_order_release);
    // g_trackingThread = std::make_unique<std::thread>(trackingThreadFunc);
    // std::cout << "[Main] Tracking thread launched (atomic coordinates)" << std::endl;

    // Run main loop (GPU/rendering only)
    mainLoop();

    // Save settings on exit
    Config& exitConfig = Config::getInstance();
    if (exitConfig.save("config/settings.ini")) {
        std::cout << "[Shutdown] Settings saved" << std::endl;
    }

    // Shutdown: Stop tracking thread
    std::cout << "\n[Main] Shutting down..." << std::endl;
    // g_trackingRunning.store(false, std::memory_order_release);
    // if (g_trackingThread && g_trackingThread->joinable()) {
    //     std::cout << "[Main] Waiting for tracking thread..." << std::endl;
    //     g_trackingThread->join();
    //     std::cout << "[Main] Tracking thread joined" << std::endl;
    // }

    // Cleanup
    cleanup();

    std::cout << "Application exited successfully" << std::endl;
    return 0;
}
