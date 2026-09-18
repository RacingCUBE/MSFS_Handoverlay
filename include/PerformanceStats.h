#pragma once

#include <atomic>
#include <iostream>
#include <chrono>

/**
 * @brief Thread-safe performance statistics tracking
 *
 * Tracks performance metrics across multiple threads using atomic operations.
 * All operations are thread-safe and lock-free.
 */
struct PerformanceStats {
    // Frame counters (per second)
    std::atomic<uint64_t> captureFrameCount{0};  ///< Frames captured by capture thread
    std::atomic<uint64_t> renderFrameCount{0};   ///< Frames rendered by main thread
    std::atomic<uint64_t> droppedFrameCount{0};  ///< Frames dropped (captured but not rendered)

    // Timing metrics (milliseconds)
    std::atomic<double> avgCaptureTimeMs{0.0};   ///< Average time to capture a frame
    std::atomic<double> avgProcessTimeMs{0.0};   ///< Average time to process (GPU chroma key)
    std::atomic<double> avgRenderTimeMs{0.0};    ///< Average time to render to VR overlay
    std::atomic<double> avgLatencyMs{0.0};       ///< Average end-to-end latency (capture to render)
    std::atomic<double> avgSegmentationTimeMs{0.0};  ///< AI Segmentation inference time (both eyes), 0 when not in AI mode

    // Timestamp for FPS calculation
    std::chrono::steady_clock::time_point startTime;

    /**
     * @brief Constructor - initializes start time
     */
    PerformanceStats() {
        startTime = std::chrono::steady_clock::now();
    }

    /**
     * @brief Reset all counters and timing metrics
     */
    void reset() {
        captureFrameCount.store(0, std::memory_order_relaxed);
        renderFrameCount.store(0, std::memory_order_relaxed);
        droppedFrameCount.store(0, std::memory_order_relaxed);
        avgCaptureTimeMs.store(0.0, std::memory_order_relaxed);
        avgProcessTimeMs.store(0.0, std::memory_order_relaxed);
        avgRenderTimeMs.store(0.0, std::memory_order_relaxed);
        avgLatencyMs.store(0.0, std::memory_order_relaxed);
        avgSegmentationTimeMs.store(0.0, std::memory_order_relaxed);
        startTime = std::chrono::steady_clock::now();
    }

    /**
     * @brief Calculate current FPS based on elapsed time
     *
     * @param frameCount Total frames counted
     * @return FPS (frames per second)
     */
    double calculateFPS(uint64_t frameCount) const {
        auto now = std::chrono::steady_clock::now();
        double elapsedSeconds = std::chrono::duration<double>(now - startTime).count();
        return (elapsedSeconds > 0.0) ? (frameCount / elapsedSeconds) : 0.0;
    }

    /**
     * @brief Print performance statistics to console
     *
     * Thread-safe: Uses atomic loads for all metrics
     */
    void printStats() const {
        uint64_t capturedFrames = captureFrameCount.load(std::memory_order_relaxed);
        uint64_t renderedFrames = renderFrameCount.load(std::memory_order_relaxed);
        uint64_t droppedFrames = droppedFrameCount.load(std::memory_order_relaxed);

        double captureFPS = calculateFPS(capturedFrames);
        double renderFPS = calculateFPS(renderedFrames);

        std::cout << "\n=== Performance Stats ===" << std::endl;
        std::cout << "Capture FPS:     " << captureFPS << " (" << capturedFrames << " frames)" << std::endl;
        std::cout << "Render FPS:      " << renderFPS << " (" << renderedFrames << " frames)" << std::endl;
        std::cout << "Dropped Frames:  " << droppedFrames << std::endl;
        std::cout << "Avg Capture:     " << avgCaptureTimeMs.load(std::memory_order_relaxed) << " ms" << std::endl;
        std::cout << "Avg Process:     " << avgProcessTimeMs.load(std::memory_order_relaxed) << " ms" << std::endl;
        std::cout << "Avg Render:      " << avgRenderTimeMs.load(std::memory_order_relaxed) << " ms" << std::endl;
        std::cout << "Avg Latency:     " << avgLatencyMs.load(std::memory_order_relaxed) << " ms" << std::endl;
        std::cout << "Avg Segmentation:" << avgSegmentationTimeMs.load(std::memory_order_relaxed) << " ms" << std::endl;
        std::cout << "=========================" << std::endl;
    }

    /**
     * @brief Print compact one-line stats for continuous monitoring
     */
    void printCompact() const {
        uint64_t capturedFrames = captureFrameCount.load(std::memory_order_relaxed);
        uint64_t renderedFrames = renderFrameCount.load(std::memory_order_relaxed);

        double captureFPS = calculateFPS(capturedFrames);
        double renderFPS = calculateFPS(renderedFrames);

        std::cout << "[Stats] Capture: " << captureFPS << " FPS, "
                  << "Render: " << renderFPS << " FPS, "
                  << "Latency: " << avgLatencyMs.load(std::memory_order_relaxed) << " ms"
                  << std::endl;
    }

    /**
     * @brief Update dropped frame count based on capture/render difference
     *
     * Call this periodically to track dropped frames
     */
    void updateDroppedFrames() {
        uint64_t captured = captureFrameCount.load(std::memory_order_relaxed);
        uint64_t rendered = renderFrameCount.load(std::memory_order_relaxed);

        if (captured > rendered) {
            uint64_t dropped = captured - rendered;
            droppedFrameCount.store(dropped, std::memory_order_relaxed);
        }
    }
};
