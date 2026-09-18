#pragma once

#include <opencv2/opencv.hpp>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>
#include <string>
#include <vector>

// Result of probing one camera index: whether it opened, and what resolution it delivered.
struct CameraScanEntry {
    int index = 0;
    bool opened = false;
    int width = 0;
    int height = 0;
};

/**
 * @brief Dual-camera capture with per-camera threads to prevent buffer buildup
 *
 * Each camera runs in its own thread with grab() loops to dump backlog.
 * This prevents Camera B from waiting on Camera A's slower internal clock.
 */
class CameraCapture {
public:
    CameraCapture();
    ~CameraCapture();

    bool initialize(int leftCameraIndex, int rightCameraIndex,
                   int width, int height, int fps);

    // Switch capture source to the Valve Index built-in stereo camera via
    // OpenVR's IVRTrackedCamera. Frames are split horizontally (left half = left
    // eye, right half = right eye) into m_leftLatestFrame / m_rightLatestFrame
    // so getLatestFrames / hasNewFrames behave identically to OpenCV mode.
    bool initializeValveBuiltInCamera();

    bool getLatestFrames(cv::Mat& leftFrame, cv::Mat& rightFrame);
    bool hasNewFrames(uint64_t& lastLeftSeq, uint64_t& lastRightSeq);
    void release();

    bool isInitialized() const { return m_initialized.load(std::memory_order_acquire); }

    // Human-readable description of the last failure (empty on success).
    // Safe to read after initialize / initializeValveBuiltInCamera returns false.
    const std::string& getLastError() const { return m_lastError; }

    // Get current sequence numbers without updating (for backlog detection)
    uint64_t getLeftSeq() const { return m_leftFrameSeq.load(std::memory_order_acquire); }
    uint64_t getRightSeq() const { return m_rightFrameSeq.load(std::memory_order_acquire); }

    // Valve Index built-in camera diagnostics: the raw combined frame size reported
    // by OpenVR, and the per-eye slice size before it gets resized to the configured
    // frame size - lets the UI show whether the top/bottom split assumption is right
    // without needing console/log access.
    uint32_t getValveRawWidth() const { return m_valveFrameWidth; }
    uint32_t getValveRawHeight() const { return m_valveFrameHeight; }
    int getValveSliceWidth() const { return m_valveSliceWidth.load(std::memory_order_relaxed); }
    int getValveSliceHeight() const { return m_valveSliceHeight.load(std::memory_order_relaxed); }

    // If initialize() had to auto-substitute a different index than configured (because the
    // configured one didn't open or delivered a resolution that clearly isn't a real webcam,
    // e.g. a VR headset's own tracking camera), this returns that index. -1 if no substitution
    // happened (the configured index was used as-is, or initialize() hasn't run/succeeded).
    int getLeftAutoSelectedIndex() const { return m_leftAutoSelectedIndex; }
    int getRightAutoSelectedIndex() const { return m_rightAutoSelectedIndex; }

    // Background scan of camera indices 0..maxIndex: opens each briefly (short timeout,
    // no side effects on ongoing capture) and records whether it opened and at what
    // resolution, so the UI can show "what's actually at each index" instead of blind
    // trial and error. Safe to call regardless of whether initialize() has run.
    void beginCameraScan(int maxIndex = 9);
    bool isCameraScanRunning() const { return m_scanInProgress.load(std::memory_order_acquire); }
    std::vector<CameraScanEntry> getCameraScanResults();

private:
    // Camera hardware
    cv::VideoCapture m_leftCamera;
    cv::VideoCapture m_rightCamera;

    // Thread coordination
    std::atomic<bool> m_initialized{false};
    std::atomic<bool> m_running{false};
    std::unique_ptr<std::thread> m_leftThread;
    std::unique_ptr<std::thread> m_rightThread;
    std::unique_ptr<std::thread> m_valveThread;

    // Valve Index built-in camera state (OpenVR IVRTrackedCamera)
    // vr::TrackedCameraHandle_t is a uint64_t; keep header free of openvr.h.
    bool m_usingValveCamera = false;
    bool m_ownsOpenVRSession = false;
    uint64_t m_valveCameraHandle = 0;
    uint32_t m_valveFrameWidth = 0;
    uint32_t m_valveFrameHeight = 0;
    uint32_t m_valveFrameBufferSize = 0;
    // vr::EVRTrackedCameraFrameType of whichever frame type GetCameraFrameSize
    // actually succeeded with (not all headsets/drivers support "Distorted").
    uint32_t m_valveFrameType = 0;
    // Per-eye slice size before resizing, for on-screen diagnostics.
    std::atomic<int> m_valveSliceWidth{0};
    std::atomic<int> m_valveSliceHeight{0};

    std::string m_lastError;

    // Latest frames (protected by mutexes)
    cv::Mat m_leftLatestFrame;
    cv::Mat m_rightLatestFrame;
    std::mutex m_leftFrameMutex;
    std::mutex m_rightFrameMutex;

    // Frame sequence tracking to detect if we're re-processing same frame
    std::atomic<uint64_t> m_leftFrameSeq{0};
    std::atomic<uint64_t> m_rightFrameSeq{0};

    // -1 = configured index was used as-is; otherwise the auto-substituted index (see
    // getLeftAutoSelectedIndex/getRightAutoSelectedIndex).
    int m_leftAutoSelectedIndex = -1;
    int m_rightAutoSelectedIndex = -1;

    // Background camera scan state
    std::atomic<bool> m_scanInProgress{false};
    std::mutex m_scanResultsMutex;
    std::vector<CameraScanEntry> m_scanResults;
    std::unique_ptr<std::thread> m_scanThread;

    bool openCamera(cv::VideoCapture& camera, int index,
                   int width, int height, int fps);

    // Searches indices starting with preferredIndex (then 0..maxIndex) for one that opens
    // and delivers a resolution that looks like a real webcam for the given target size,
    // skipping excludeIndex (already claimed by the other eye). Returns -1 if none found.
    // Uses a short per-index timeout, distinct from openCamera()'s slower retry logic, so
    // searching through several wrong indices doesn't stall startup for a long time.
    int findBestCameraIndex(int preferredIndex, int excludeIndex,
                             int targetWidth, int targetHeight, int maxIndex);

    void leftCameraThreadFunc();
    void rightCameraThreadFunc();
    void valveCameraThreadFunc();
};
