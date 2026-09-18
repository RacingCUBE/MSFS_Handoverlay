#include "CameraCapture.h"
#include "Config.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>
#include <memory>
#include <algorithm>
#include <cmath>
#include <openvr.h>

namespace {

// Detects a horizontal frame-tear (two unrelated captures jammed together in one frame,
// from unstable USB/MJPEG timing) by finding the column with an abnormally large pixel
// discontinuity relative to the frame's typical column-to-column change. Returns the
// detected split point, or 0 if nothing stands out as a genuine tear (vs. ordinary scene
// content like edges/objects). This replaces a previously hardcoded SplitOffsetPixels
// value, which turned out to vary between camera connections rather than being a fixed
// geometric defect - a fresh detection is needed each time the camera (re)connects.
int detectFrameTearSplit(const cv::Mat& bgrFrame) {
    int cols = bgrFrame.cols;
    int rows = bgrFrame.rows;
    int margin = cols / 20;  // ignore near the edges - a tear there is negligible anyway

    std::vector<double> colDiff(cols, 0.0);
    for (int x = 1; x < cols; ++x) {
        double diff = 0.0;
        for (int y = 0; y < rows; y += 4) {  // sample every 4th row for speed
            const cv::Vec3b& a = bgrFrame.at<cv::Vec3b>(y, x - 1);
            const cv::Vec3b& b = bgrFrame.at<cv::Vec3b>(y, x);
            diff += std::abs(a[0] - b[0]) + std::abs(a[1] - b[1]) + std::abs(a[2] - b[2]);
        }
        colDiff[x] = diff;
    }

    std::vector<double> sorted = colDiff;
    std::sort(sorted.begin(), sorted.end());
    double median = sorted[sorted.size() / 2];

    int bestCol = -1;
    double bestDiff = 0.0;
    for (int x = margin; x < cols - margin; ++x) {
        if (colDiff[x] > bestDiff) { bestDiff = colDiff[x]; bestCol = x; }
    }

    // Only treat it as a genuine tear if it's a dramatic outlier vs. typical content edges
    if (bestCol < 0 || median <= 0.0 || bestDiff < median * 15.0) {
        return 0;
    }
    return bestCol;
}

}  // namespace

namespace {
// cv::VideoCapture::open() can hang indefinitely on some DirectShow/MSMF backend +
// device combinations (observed: a second, older UVC webcam alongside a modern one -
// MSMF hangs enumerating/negotiating it instead of failing). Opening on a worker thread
// and only waiting up to timeoutMs turns that hang into a clean failure instead of
// freezing the whole app. If the backend call never returns, the worker thread and its
// own VideoCapture are simply abandoned (leaked) rather than risking a crash by touching
// a cv::VideoCapture that's still inside a non-cancelable OS call on another thread.
bool openCameraBackendWithTimeout(cv::VideoCapture& outCamera, int index, int apiPreference,
                                   int timeoutMs, const char* backendName) {
    auto capture = std::make_shared<cv::VideoCapture>();
    auto done = std::make_shared<std::atomic<bool>>(false);

    std::thread worker([capture, done, index, apiPreference]() {
        capture->open(index, apiPreference);
        done->store(true, std::memory_order_release);
    });
    worker.detach();

    auto start = std::chrono::steady_clock::now();
    while (!done->load(std::memory_order_acquire)) {
        if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(timeoutMs)) {
            std::cerr << "[Camera " << index << "] " << backendName << " open() timed out after "
                      << timeoutMs << "ms - backend appears stuck, giving up on this attempt"
                      << std::endl;
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    if (!capture->isOpened()) {
        return false;
    }

    outCamera = *capture;  // cv::VideoCapture is ref-counted internally; this is a cheap, safe copy
    return true;
}

// Quick check: does this index open at all, and if so, what resolution does it deliver?
// Uses a short timeout and no property configuration/priming - just enough to answer
// "what's here", not to set a camera up for real capture (that's openCamera()'s job).
bool quickProbeCamera(int index, int& outWidth, int& outHeight) {
    cv::VideoCapture cap;
    bool opened = openCameraBackendWithTimeout(cap, index, cv::CAP_DSHOW, 1200, "Probe-DirectShow");
    if (!opened) {
        opened = openCameraBackendWithTimeout(cap, index, cv::CAP_MSMF, 1200, "Probe-MSMF");
    }
    if (!opened) {
        return false;
    }

    cv::Mat frame;
    for (int attempt = 0; attempt < 5 && frame.empty(); ++attempt) {
        cap.read(frame);
        if (frame.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
        }
    }
    cap.release();

    if (frame.empty()) {
        return false;
    }
    outWidth = frame.cols;
    outHeight = frame.rows;
    return true;
}

// A resolution more than 50% off the target aspect ratio almost certainly isn't a real
// overhead webcam for this rig - e.g. a VR headset's own tracking camera (1920x960 raw)
// probed against a configured 640x480 target, or a virtual camera showing a placeholder.
bool looksLikeWrongDevice(int probeWidth, int probeHeight, int targetWidth, int targetHeight) {
    if (probeWidth <= 0 || probeHeight <= 0 || targetWidth <= 0 || targetHeight <= 0) {
        return true;
    }
    double aspectTarget = static_cast<double>(targetWidth) / static_cast<double>(targetHeight);
    double aspectProbe = static_cast<double>(probeWidth) / static_cast<double>(probeHeight);
    double deviation = std::abs(aspectProbe - aspectTarget) / aspectTarget;
    return deviation > 0.5;
}

}  // namespace

CameraCapture::CameraCapture() {
}

CameraCapture::~CameraCapture() {
    release();
    // Block until any in-progress scan finishes - it accesses this object's members,
    // so it must not still be running past this point.
    if (m_scanThread && m_scanThread->joinable()) {
        m_scanThread->join();
    }
}

void CameraCapture::beginCameraScan(int maxIndex) {
    if (m_scanInProgress.exchange(true, std::memory_order_acq_rel)) {
        return;  // a scan is already running
    }
    if (m_scanThread && m_scanThread->joinable()) {
        m_scanThread->join();
    }
    {
        std::lock_guard<std::mutex> lock(m_scanResultsMutex);
        m_scanResults.clear();
    }

    m_scanThread = std::make_unique<std::thread>([this, maxIndex]() {
        for (int i = 0; i <= maxIndex; ++i) {
            CameraScanEntry entry;
            entry.index = i;
            int w = 0, h = 0;
            if (quickProbeCamera(i, w, h)) {
                entry.opened = true;
                entry.width = w;
                entry.height = h;
            }
            {
                std::lock_guard<std::mutex> lock(m_scanResultsMutex);
                m_scanResults.push_back(entry);
            }
        }
        m_scanInProgress.store(false, std::memory_order_release);
    });
}

std::vector<CameraScanEntry> CameraCapture::getCameraScanResults() {
    std::lock_guard<std::mutex> lock(m_scanResultsMutex);
    return m_scanResults;
}

int CameraCapture::findBestCameraIndex(int preferredIndex, int excludeIndex,
                                        int targetWidth, int targetHeight, int maxIndex) {
    std::vector<int> order;
    order.push_back(preferredIndex);
    for (int i = 0; i <= maxIndex; ++i) {
        if (i != preferredIndex) {
            order.push_back(i);
        }
    }

    for (int idx : order) {
        if (idx == excludeIndex) {
            continue;
        }
        int w = 0, h = 0;
        if (!quickProbeCamera(idx, w, h)) {
            continue;
        }
        if (looksLikeWrongDevice(w, h, targetWidth, targetHeight)) {
            std::cout << "[CameraCapture] Index " << idx << " opened but delivered " << w << "x" << h
                      << ", which doesn't look right for a " << targetWidth << "x" << targetHeight
                      << " webcam - skipping" << std::endl;
            continue;
        }
        return idx;
    }
    return -1;
}

void CameraCapture::leftCameraThreadFunc() {
    cv::Mat frame;
    Config& config = Config::getInstance();
    int frameCount = 0;

    std::cout << "[LeftCamera] Thread started" << std::endl;

    while (m_running.load(std::memory_order_acquire)) {
        // Use read() instead of grab/retrieve for simplicity
        if (!m_leftCamera.read(frame)) {
            if (frameCount == 0) {
                std::cerr << "[LeftCamera] Initial read failed - no frames available" << std::endl;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // Got a frame - now process it
        if (!frame.empty()) {
            // Manual frame-tear split fix for the left camera only (config.camera.splitOffsetPixels).
            // Unlike the right camera's auto-detected split, this is a plain user-toggled value -
            // auto-detection isn't reliable for this camera, so it's a manual on/off correction instead.
            cv::Mat swapped;
            int splitAt = config.camera.splitOffsetPixels;
            if (splitAt > 0 && splitAt < frame.cols) {
                swapped.create(frame.rows, frame.cols, frame.type());
                cv::Mat leftPart = frame(cv::Rect(0, 0, splitAt, frame.rows));
                cv::Mat rightPart = frame(cv::Rect(splitAt, 0, frame.cols - splitAt, frame.rows));
                rightPart.copyTo(swapped(cv::Rect(0, 0, frame.cols - splitAt, frame.rows)));
                leftPart.copyTo(swapped(cv::Rect(frame.cols - splitAt, 0, splitAt, frame.rows)));
            } else {
                swapped = frame;
            }

            // Process left frame with pixel offset
            cv::Mat processed;

            // Apply pixel offset if configured (for camera alignment)
            if (config.camera.leftPixelOffsetX != 0 || config.camera.leftPixelOffsetY != 0) {
                cv::Mat translationMat = (cv::Mat_<float>(2, 3) <<
                    1, 0, config.camera.leftPixelOffsetX,
                    0, 1, config.camera.leftPixelOffsetY);
                cv::warpAffine(swapped, processed, translationMat, swapped.size(), cv::INTER_CUBIC);
            } else {
                processed = swapped.clone();
            }

            // Update latest frame (mutex-protected) - OVERWRITE, not queue
            {
                std::lock_guard<std::mutex> lock(m_leftFrameMutex);
                m_leftLatestFrame = processed;  // Overwrites old frame = always latest
            }

            // Increment sequence number (atomic) - main thread can detect new frames
            m_leftFrameSeq.fetch_add(1, std::memory_order_release);

            frameCount++;
            if (frameCount == 1) {
                std::cout << "[LeftCamera] First frame captured! Size: " << processed.cols << "x" << processed.rows << std::endl;
            }
        }
    }

    std::cout << "[LeftCamera] Thread exiting. Total frames: " << frameCount << std::endl;
}

void CameraCapture::rightCameraThreadFunc() {
    cv::Mat frame;
    Config& config = Config::getInstance();
    int frameCount = 0;
    int detectedSplitAt = -1;  // -1 = not yet detected for this camera connection

    std::cout << "[RightCamera] Thread started" << std::endl;

    while (m_running.load(std::memory_order_acquire)) {
        // Use read() instead of grab/retrieve for simplicity
        if (!m_rightCamera.read(frame)) {
            if (frameCount == 0) {
                std::cerr << "[RightCamera] Initial read failed - no frames available" << std::endl;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // Got a frame - now process it
        if (!frame.empty()) {
            // FIX: this camera sometimes outputs a torn frame (two captures jammed together)
            // due to unstable USB/MJPEG timing. The tear position varies per connection, so it's
            // auto-detected once from the first real frame rather than using a fixed config value.
            if (detectedSplitAt < 0) {
                detectedSplitAt = detectFrameTearSplit(frame);
                std::cout << "[RightCamera] Auto-detected frame tear split: " << detectedSplitAt
                          << " px" << (detectedSplitAt == 0 ? " (none found)" : "") << std::endl;
            }

            cv::Mat swapped(frame.rows, frame.cols, frame.type());
            int splitAt = detectedSplitAt;
            if (splitAt > 0 && splitAt < frame.cols) {
                cv::Mat leftPart = frame(cv::Rect(0, 0, splitAt, frame.rows));
                cv::Mat rightPart = frame(cv::Rect(splitAt, 0, frame.cols - splitAt, frame.rows));
                rightPart.copyTo(swapped(cv::Rect(0, 0, frame.cols - splitAt, frame.rows)));
                leftPart.copyTo(swapped(cv::Rect(frame.cols - splitAt, 0, splitAt, frame.rows)));
            } else {
                swapped = frame.clone();
            }

            // Apply pixel offset if configured (for camera alignment)
            cv::Mat processed;
            if (config.camera.rightPixelOffsetX != 0 || config.camera.rightPixelOffsetY != 0) {
                cv::Mat translationMat = (cv::Mat_<float>(2, 3) <<
                    1, 0, config.camera.rightPixelOffsetX,
                    0, 1, config.camera.rightPixelOffsetY);
                cv::warpAffine(swapped, processed, translationMat, swapped.size(), cv::INTER_CUBIC);
            } else {
                processed = swapped;
            }

            // Update latest frame (mutex-protected)
            {
                std::lock_guard<std::mutex> lock(m_rightFrameMutex);
                m_rightLatestFrame = processed;
            }

            // Increment sequence number (atomic) - main thread can detect new frames
            m_rightFrameSeq.fetch_add(1, std::memory_order_release);

            frameCount++;
            if (frameCount == 1) {
                std::cout << "[RightCamera] First frame captured! Size: " << processed.cols << "x" << processed.rows << std::endl;
            }
        }
    }

    std::cout << "[RightCamera] Thread exiting. Total frames: " << frameCount << std::endl;
}

void CameraCapture::valveCameraThreadFunc() {
    using namespace vr;

    IVRTrackedCamera* trackedCam = VRTrackedCamera();
    if (!trackedCam) {
        std::cerr << "[ValveCamera] IVRTrackedCamera interface not available" << std::endl;
        return;
    }

    Config& config = Config::getInstance();
    std::vector<uint8_t> rgbaBuffer(m_valveFrameBufferSize);
    CameraVideoStreamFrameHeader_t header{};
    bool gotFirstFrame = false;
    uint32_t lastFrameSeq = 0;
    int frameCount = 0;

    std::cout << "[ValveCamera] Thread started ("
              << m_valveFrameWidth << "x" << m_valveFrameHeight << ")" << std::endl;

    while (m_running.load(std::memory_order_acquire)) {
        EVRTrackedCameraError err = trackedCam->GetVideoStreamFrameBuffer(
            static_cast<TrackedCameraHandle_t>(m_valveCameraHandle),
            static_cast<EVRTrackedCameraFrameType>(m_valveFrameType),
            rgbaBuffer.data(),
            static_cast<uint32_t>(rgbaBuffer.size()),
            &header,
            sizeof(header));

        if (err != VRTrackedCameraError_None) {
            // No frame yet, or transient error — back off briefly and retry.
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
            continue;
        }

        // Skip duplicates *after* we've published a first frame — the very
        // first call may legitimately report sequence 0, which equals our
        // initial lastFrameSeq sentinel and would otherwise be skipped.
        if (gotFirstFrame && header.nFrameSequence == lastFrameSeq) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        lastFrameSeq = header.nFrameSequence;
        gotFirstFrame = true;

        // Wrap the RGBA buffer in a cv::Mat (no copy), convert to BGR.
        cv::Mat rgba(static_cast<int>(m_valveFrameHeight),
                     static_cast<int>(m_valveFrameWidth),
                     CV_8UC4,
                     rgbaBuffer.data());
        cv::Mat bgr;
        cv::cvtColor(rgba, bgr, cv::COLOR_RGBA2BGR);

        // Index dual-camera frames are placed side by side horizontally:
        // left half = left eye camera, right half = right eye camera.
        // (Confirmed on this rig: a 1920x960 raw frame is two 960x960 square-ish
        // eye views side by side, not two 1920x480 letterbox strips stacked.)
        int halfW = bgr.cols / 2;
        if (halfW <= 0) {
            continue;
        }
        cv::Mat leftSlice = bgr(cv::Rect(0, 0, halfW, bgr.rows));
        cv::Mat rightSlice = bgr(cv::Rect(halfW, 0, halfW, bgr.rows));
        m_valveSliceWidth.store(leftSlice.cols, std::memory_order_relaxed);
        m_valveSliceHeight.store(leftSlice.rows, std::memory_order_relaxed);

        // Resize each eye to the configured per-eye size so the downstream
        // render/overlay pipeline (which uses config.camera.frameWidth/Height)
        // sees frames of the expected dimensions.
        int targetW = config.camera.frameWidth;
        int targetH = config.camera.frameHeight;
        cv::Mat leftResized, rightResized;
        if (targetW > 0 && targetH > 0 &&
            (leftSlice.cols != targetW || leftSlice.rows != targetH)) {
            cv::resize(leftSlice, leftResized, cv::Size(targetW, targetH), 0, 0, cv::INTER_AREA);
            cv::resize(rightSlice, rightResized, cv::Size(targetW, targetH), 0, 0, cv::INTER_AREA);
        } else {
            leftResized = leftSlice.clone();
            rightResized = rightSlice.clone();
        }

        // Apply the same per-eye pixel offset adjustment used for external cameras,
        // so the Left/Right Camera Offset controls work identically in this mode.
        cv::Mat leftOut;
        if (config.camera.leftPixelOffsetX != 0 || config.camera.leftPixelOffsetY != 0) {
            cv::Mat translationMat = (cv::Mat_<float>(2, 3) <<
                1, 0, config.camera.leftPixelOffsetX,
                0, 1, config.camera.leftPixelOffsetY);
            cv::warpAffine(leftResized, leftOut, translationMat, leftResized.size(), cv::INTER_CUBIC);
        } else {
            leftOut = leftResized;
        }

        cv::Mat rightOut;
        if (config.camera.rightPixelOffsetX != 0 || config.camera.rightPixelOffsetY != 0) {
            cv::Mat translationMat = (cv::Mat_<float>(2, 3) <<
                1, 0, config.camera.rightPixelOffsetX,
                0, 1, config.camera.rightPixelOffsetY);
            cv::warpAffine(rightResized, rightOut, translationMat, rightResized.size(), cv::INTER_CUBIC);
        } else {
            rightOut = rightResized;
        }

        {
            std::lock_guard<std::mutex> lock(m_leftFrameMutex);
            m_leftLatestFrame = leftOut;
        }
        m_leftFrameSeq.fetch_add(1, std::memory_order_release);

        {
            std::lock_guard<std::mutex> lock(m_rightFrameMutex);
            m_rightLatestFrame = rightOut;
        }
        m_rightFrameSeq.fetch_add(1, std::memory_order_release);

        frameCount++;
        if (frameCount == 1) {
            std::cout << "[ValveCamera] First frame captured! Source per-eye: "
                      << leftSlice.cols << "x" << leftSlice.rows
                      << " -> published " << leftOut.cols << "x" << leftOut.rows
                      << std::endl;
        }
    }

    std::cout << "[ValveCamera] Thread exiting. Total frames: " << frameCount << std::endl;
}

bool CameraCapture::initializeValveBuiltInCamera() {
    using namespace vr;

    release();
    m_lastError.clear();

    auto fail = [&](const std::string& msg) {
        m_lastError = msg;
        std::cerr << "[ValveCamera] " << msg << std::endl;
        if (m_ownsOpenVRSession) { VR_Shutdown(); m_ownsOpenVRSession = false; }
        return false;
    };

    std::cout << "[CameraCapture] Switching to Valve built-in stereo camera..." << std::endl;

    if (!VR_IsRuntimeInstalled()) {
        return fail("OpenVR runtime not installed (is SteamVR installed?)");
    }

    if (!VRSystem()) {
        EVRInitError initErr = VRInitError_None;
        VR_Init(&initErr, VRApplication_Background);
        if (initErr != VRInitError_None) {
            return fail(std::string("VR_Init failed: ") +
                        VR_GetVRInitErrorAsEnglishDescription(initErr) +
                        " (start SteamVR first)");
        }
        m_ownsOpenVRSession = true;
    }

    IVRTrackedCamera* trackedCam = VRTrackedCamera();
    if (!trackedCam) {
        return fail("IVRTrackedCamera interface unavailable");
    }

    bool hasCamera = false;
    EVRTrackedCameraError camErr =
        trackedCam->HasCamera(k_unTrackedDeviceIndex_Hmd, &hasCamera);
    if (camErr != VRTrackedCameraError_None || !hasCamera) {
        return fail(std::string("HMD reports no usable camera (") +
                    trackedCam->GetCameraErrorNameFromEnum(camErr) +
                    "). Enable Camera in SteamVR Settings.");
    }

    // Not every headset/driver supports the raw "Distorted" frame type through
    // this API - try it first (matches the raw sensor layout GetVideoStreamFrameBuffer
    // expects below), then fall back to the undistorted variants.
    const EVRTrackedCameraFrameType frameTypesToTry[] = {
        VRTrackedCameraFrameType_Distorted,
        VRTrackedCameraFrameType_Undistorted,
        VRTrackedCameraFrameType_MaximumUndistorted,
    };
    bool gotFrameSize = false;
    for (EVRTrackedCameraFrameType frameType : frameTypesToTry) {
        camErr = trackedCam->GetCameraFrameSize(
            k_unTrackedDeviceIndex_Hmd,
            frameType,
            &m_valveFrameWidth,
            &m_valveFrameHeight,
            &m_valveFrameBufferSize);
        if (camErr == VRTrackedCameraError_None && m_valveFrameBufferSize > 0) {
            m_valveFrameType = static_cast<uint32_t>(frameType);
            gotFrameSize = true;
            break;
        }
    }
    if (!gotFrameSize) {
        return fail(std::string("GetCameraFrameSize failed: ") +
                    trackedCam->GetCameraErrorNameFromEnum(camErr));
    }

    TrackedCameraHandle_t handle = INVALID_TRACKED_CAMERA_HANDLE;
    camErr = trackedCam->AcquireVideoStreamingService(k_unTrackedDeviceIndex_Hmd, &handle);
    if (camErr != VRTrackedCameraError_None || handle == INVALID_TRACKED_CAMERA_HANDLE) {
        return fail(std::string("AcquireVideoStreamingService failed: ") +
                    trackedCam->GetCameraErrorNameFromEnum(camErr));
    }
    m_valveCameraHandle = static_cast<uint64_t>(handle);
    m_usingValveCamera = true;

    std::cout << "[CameraCapture] Valve camera stream acquired ("
              << m_valveFrameWidth << "x" << m_valveFrameHeight
              << ", " << m_valveFrameBufferSize << " bytes/frame)" << std::endl;

    m_running.store(true, std::memory_order_release);
    m_valveThread = std::make_unique<std::thread>(&CameraCapture::valveCameraThreadFunc, this);
    m_initialized.store(true, std::memory_order_release);

    for (int i = 0; i < 50; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::lock_guard<std::mutex> lock(m_leftFrameMutex);
        if (!m_leftLatestFrame.empty()) {
            std::cout << "[CameraCapture] Valve camera streaming." << std::endl;
            return true;
        }
    }

    m_lastError = "No frames received within 5s (camera may be disabled in SteamVR)";
    std::cerr << "[ValveCamera] " << m_lastError << std::endl;
    release();
    return false;
}

bool CameraCapture::initialize(int leftCameraIndex, int rightCameraIndex,
                               int width, int height, int fps) {
    release();
    m_leftAutoSelectedIndex = -1;
    m_rightAutoSelectedIndex = -1;

    std::cout << "[CameraCapture] Initializing cameras..." << std::endl;
    std::cout << "[CameraCapture] Left camera: " << leftCameraIndex << std::endl;
    std::cout << "[CameraCapture] Right camera: " << rightCameraIndex << std::endl;

    // Quick-probe the configured indices (and, if needed, search 0-9) before doing the real,
    // slower open - catches the common "index shifted / grabbed the wrong device" case (e.g.
    // a VR headset's own tracking camera) without a long timeout-per-wrong-index cost.
    int resolvedLeft = findBestCameraIndex(leftCameraIndex, -1, width, height, 9);
    if (resolvedLeft < 0) {
        m_lastError = "No suitable left camera found (checked indices 0-9)";
        std::cerr << "[CameraCapture] " << m_lastError << std::endl;
        return false;
    }
    if (resolvedLeft != leftCameraIndex) {
        m_leftAutoSelectedIndex = resolvedLeft;
        std::cout << "[CameraCapture] Left camera: configured index " << leftCameraIndex
                   << " looked wrong or unavailable, auto-selected index " << resolvedLeft
                   << " instead" << std::endl;
    }

    int resolvedRight = findBestCameraIndex(rightCameraIndex, resolvedLeft, width, height, 9);
    if (resolvedRight < 0) {
        m_lastError = "No suitable right camera found (checked indices 0-9)";
        std::cerr << "[CameraCapture] " << m_lastError << std::endl;
        return false;
    }
    if (resolvedRight != rightCameraIndex) {
        m_rightAutoSelectedIndex = resolvedRight;
        std::cout << "[CameraCapture] Right camera: configured index " << rightCameraIndex
                   << " looked wrong or unavailable, auto-selected index " << resolvedRight
                   << " instead" << std::endl;
    }

    if (!openCamera(m_leftCamera, resolvedLeft, width, height, fps)) {
        m_lastError = "Failed to open left camera (index " + std::to_string(resolvedLeft) + ")";
        std::cerr << "[CameraCapture] " << m_lastError << std::endl;
        return false;
    }

    if (!openCamera(m_rightCamera, resolvedRight, width, height, fps)) {
        m_lastError = "Failed to open right camera (index " + std::to_string(resolvedRight) + ")";
        std::cerr << "[CameraCapture] " << m_lastError << std::endl;
        m_leftCamera.release();
        return false;
    }

    // Start camera threads
    m_running.store(true, std::memory_order_release);
    m_leftThread = std::make_unique<std::thread>(&CameraCapture::leftCameraThreadFunc, this);
    m_rightThread = std::make_unique<std::thread>(&CameraCapture::rightCameraThreadFunc, this);

    m_initialized.store(true, std::memory_order_release);
    std::cout << "[CameraCapture] Cameras initialized and threads started" << std::endl;
    std::cout << "[CameraCapture] Waiting for first frames..." << std::endl;

    // Wait for first frames to be captured (up to 5 seconds)
    for (int i = 0; i < 50; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        bool leftReady = false;
        bool rightReady = false;

        {
            std::lock_guard<std::mutex> leftLock(m_leftFrameMutex);
            leftReady = !m_leftLatestFrame.empty();
        }

        {
            std::lock_guard<std::mutex> rightLock(m_rightFrameMutex);
            rightReady = !m_rightLatestFrame.empty();
        }

        if (leftReady && rightReady) {
            std::cout << "[CameraCapture] First frames captured successfully!" << std::endl;
            return true;
        }

        if (i == 29) {
            std::cerr << "[CameraCapture] Still waiting for frames... ("
                      << (leftReady ? "left OK" : "NO left") << ", "
                      << (rightReady ? "right OK" : "NO right") << ")" << std::endl;
        }
    }

    bool leftReady = false, rightReady = false;
    {
        std::lock_guard<std::mutex> leftLock(m_leftFrameMutex);
        leftReady = !m_leftLatestFrame.empty();
    }
    {
        std::lock_guard<std::mutex> rightLock(m_rightFrameMutex);
        rightReady = !m_rightLatestFrame.empty();
    }

    std::cerr << "[CameraCapture] ERROR: No frames captured after 5 seconds!" << std::endl;
    std::cerr << "[CameraCapture]   Left camera:  " << (leftReady ? "OK" : "FAILED - no frames") << std::endl;
    std::cerr << "[CameraCapture]   Right camera: " << (rightReady ? "OK" : "FAILED - no frames") << std::endl;
    std::cerr << "[CameraCapture]   Check camera indices in settings.ini (Left=" << leftCameraIndex
              << ", Right=" << rightCameraIndex << ")" << std::endl;

    release();
    return false;
}

bool CameraCapture::openCamera(cv::VideoCapture& camera, int index,
                               int width, int height, int fps) {
    // Try DirectShow backend first (Windows), fall back to MSMF. Each attempt is bounded
    // by a timeout (see openCameraBackendWithTimeout) since either backend can hang
    // indefinitely instead of failing for certain devices.
    //
    // Known OpenCV/DirectShow quirk: when two VideoCapture objects are opened by index in
    // the same process, whichever opens *second* frequently fails with "can't be used to
    // capture by index" - confirmed here by swapping open order and seeing the failure
    // follow whichever camera opened second, regardless of which physical device it was.
    // A short pause + one DirectShow retry is the documented workaround (lets DirectShow's
    // device enumerator catch up) before falling back to the (on this machine, unreliable) MSMF backend.
    bool opened = openCameraBackendWithTimeout(camera, index, cv::CAP_DSHOW, 4000, "DirectShow");

    if (!opened) {
        std::cout << "[Camera " << index << "] DirectShow failed, retrying once..." << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(750));
        opened = openCameraBackendWithTimeout(camera, index, cv::CAP_DSHOW, 4000, "DirectShow (retry)");
    }

    if (!opened) {
        std::cout << "[Camera " << index << "] DirectShow retry failed, trying MSMF backend..." << std::endl;
        opened = openCameraBackendWithTimeout(camera, index, cv::CAP_MSMF, 4000, "MSMF");
    }

    if (!opened) {
        std::cerr << "Failed to open camera " << index << " (tried DirectShow and MSMF)" << std::endl;
        return false;
    }

    // Force MJPEG codec - prevents split/swap issue on some cameras
    camera.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));

    // Set camera properties
    camera.set(cv::CAP_PROP_FRAME_WIDTH, width);
    camera.set(cv::CAP_PROP_FRAME_HEIGHT, height);
    camera.set(cv::CAP_PROP_FPS, fps);

    // CRITICAL: Minimize buffer to reduce latency and prevent burst
    camera.set(cv::CAP_PROP_BUFFERSIZE, 1);

    // Disable auto white balance/focus for consistent chroma key lighting. Exposure is left
    // on auto (3): manual mode was previously set with no exposure level ever assigned, which
    // left the camera at whatever raw hardware default it happened to power up with - often
    // near-black. Auto-exposure can drift slightly as green-screen coverage changes, but a
    // visible picture with occasional re-tuning beats a guaranteed-black one.
    camera.set(cv::CAP_PROP_AUTO_EXPOSURE, 3);   // 1 = manual, 3 = auto (DirectShow)
    camera.set(cv::CAP_PROP_AUTO_WB, 0);         // Disable auto white balance
    camera.set(cv::CAP_PROP_AUTOFOCUS, 0);        // Disable autofocus

    // Verify auto settings
    double autoExp = camera.get(cv::CAP_PROP_AUTO_EXPOSURE);
    double autoWB = camera.get(cv::CAP_PROP_AUTO_WB);
    double autoFocus = camera.get(cv::CAP_PROP_AUTOFOCUS);
    std::cout << "Camera " << index << " auto settings: exposure=" << autoExp
              << " WB=" << autoWB << " focus=" << autoFocus << std::endl;

    // Verify settings
    int actualWidth = static_cast<int>(camera.get(cv::CAP_PROP_FRAME_WIDTH));
    int actualHeight = static_cast<int>(camera.get(cv::CAP_PROP_FRAME_HEIGHT));
    int actualFPS = static_cast<int>(camera.get(cv::CAP_PROP_FPS));

    std::cout << "Camera " << index << " opened: "
              << actualWidth << "x" << actualHeight << " @ " << actualFPS << " FPS" << std::endl;

    // CRITICAL: Prime the camera by reading a few dummy frames
    // DirectShow cameras need this to start their internal buffers
    std::cout << "Camera " << index << " priming (reading dummy frames)..." << std::endl;
    cv::Mat dummyFrame;
    for (int i = 0; i < 5; i++) {
        camera.read(dummyFrame);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cout << "Camera " << index << " primed and ready" << std::endl;

    return true;
}

bool CameraCapture::hasNewFrames(uint64_t& lastLeftSeq, uint64_t& lastRightSeq) {
    uint64_t currentLeft = m_leftFrameSeq.load(std::memory_order_acquire);
    uint64_t currentRight = m_rightFrameSeq.load(std::memory_order_acquire);

    // Process if EITHER camera has new frame (not both required)
    bool hasNew = (currentLeft > lastLeftSeq) || (currentRight > lastRightSeq);
    if (hasNew) {
        lastLeftSeq = currentLeft;
        lastRightSeq = currentRight;
    }

    return hasNew;
}

bool CameraCapture::getLatestFrames(cv::Mat& leftFrame, cv::Mat& rightFrame) {
    if (!m_initialized.load(std::memory_order_acquire)) {
        return false;
    }

    // Non-blocking: grab latest frames from each camera thread
    bool leftValid = false;
    bool rightValid = false;

    {
        std::lock_guard<std::mutex> lock(m_leftFrameMutex);
        if (!m_leftLatestFrame.empty()) {
            leftFrame = m_leftLatestFrame.clone();
            leftValid = true;
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_rightFrameMutex);
        if (!m_rightLatestFrame.empty()) {
            rightFrame = m_rightLatestFrame.clone();
            rightValid = true;
        }
    }

    return leftValid && rightValid;
}

void CameraCapture::release() {
    // Signal threads to stop
    m_running.store(false, std::memory_order_release);
    m_initialized.store(false, std::memory_order_release);

    // Wait for threads to finish
    if (m_leftThread && m_leftThread->joinable()) {
        std::cout << "[CameraCapture] Waiting for left camera thread..." << std::endl;
        m_leftThread->join();
    }
    if (m_rightThread && m_rightThread->joinable()) {
        std::cout << "[CameraCapture] Waiting for right camera thread..." << std::endl;
        m_rightThread->join();
    }
    if (m_valveThread && m_valveThread->joinable()) {
        std::cout << "[CameraCapture] Waiting for Valve camera thread..." << std::endl;
        m_valveThread->join();
    }
    m_leftThread.reset();
    m_rightThread.reset();
    m_valveThread.reset();

    // Release camera resources
    if (m_leftCamera.isOpened()) {
        m_leftCamera.release();
    }
    if (m_rightCamera.isOpened()) {
        m_rightCamera.release();
    }

    // Release Valve built-in camera streaming service / OpenVR session.
    if (m_usingValveCamera) {
        if (vr::IVRTrackedCamera* trackedCam = vr::VRTrackedCamera()) {
            if (m_valveCameraHandle != 0 &&
                m_valveCameraHandle != INVALID_TRACKED_CAMERA_HANDLE) {
                trackedCam->ReleaseVideoStreamingService(
                    static_cast<vr::TrackedCameraHandle_t>(m_valveCameraHandle));
            }
        }
        m_valveCameraHandle = 0;
        m_valveFrameWidth = m_valveFrameHeight = m_valveFrameBufferSize = 0;
        m_usingValveCamera = false;
    }
    if (m_ownsOpenVRSession) {
        vr::VR_Shutdown();
        m_ownsOpenVRSession = false;
    }

    std::cout << "[CameraCapture] Released cameras and threads" << std::endl;
}
