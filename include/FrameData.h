#pragma once

#include <opencv2/opencv.hpp>
#include <chrono>
#include <cstdint>

/**
 * @brief Thread-safe frame data container for passing camera frames between threads
 *
 * This structure holds captured frames from both cameras along with timing metadata.
 * Uses deep copying (clone()) to ensure thread safety when passing between threads.
 */
struct FrameData {
    cv::Mat leftFrame;       ///< Left camera frame (BGR format)
    cv::Mat rightFrame;      ///< Right camera frame (BGR format)
    std::chrono::steady_clock::time_point captureTime;  ///< Timestamp when frame was captured
    uint64_t frameNumber;    ///< Sequential frame number for tracking

    /**
     * @brief Default constructor - initializes empty frame data
     */
    FrameData() : frameNumber(0) {}

    /**
     * @brief Deep copy constructor for thread-safe frame passing
     *
     * CRITICAL: OpenCV cv::Mat uses reference counting which is NOT thread-safe.
     * This method performs a deep copy using clone() to ensure thread safety.
     *
     * @return A fully independent copy of the frame data
     */
    FrameData clone() const {
        FrameData copy;
        copy.leftFrame = leftFrame.clone();
        copy.rightFrame = rightFrame.clone();
        copy.captureTime = captureTime;
        copy.frameNumber = frameNumber;
        return copy;
    }

    /**
     * @brief Check if frame data contains valid frames
     *
     * @return true if both left and right frames are non-empty, false otherwise
     */
    bool isValid() const {
        return !leftFrame.empty() && !rightFrame.empty();
    }

    /**
     * @brief Calculate age of frame since capture
     *
     * @return Age in milliseconds
     */
    double getAgeMs() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::milli>(now - captureTime).count();
    }
};
