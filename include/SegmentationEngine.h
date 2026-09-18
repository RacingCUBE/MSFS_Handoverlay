#pragma once

#include <opencv2/opencv.hpp>
#include <onnxruntime_cxx_api.h>
#include <string>
#include <vector>
#include <memory>

// Wraps a Robust Video Matting (RVM) ONNX model running via onnxruntime, to compute a
// per-pixel alpha matte for a camera frame - an alternative to HSV chroma-keying that
// doesn't need a green screen and avoids the spill/fringing artifacts inherent to color
// keying.
//
// RVM's ONNX contract is unusual: alongside the image ("src") it takes four recurrent
// state tensors (r1i..r4i) and returns four updated ones (r1o..r4o) to feed back in on
// the next call, plus a "downsample_ratio" scalar. This gives temporally-consistent
// (flicker-free) mattes across frames, but means inference isn't a simple stateless
// single-shot forward pass - state must persist between calls, and separately per eye
// (left/right are different physical cameras with independent scenes/parallax).
//
// Loads the model TWICE - once with the CUDA execution provider, once CPU-only - and lets
// the caller pick which to actually run against per call (preferGpu in computeAlpha).
// This exists because GPU inference gives the best matte quality but directly competes
// with MSFS's own rendering for the same GPU, causing severe FPS drops specifically while
// MSFS is focused/actively rendering VR (confirmed empirically); CPU inference has no such
// contention but visibly lower quality. Switching per-frame based on whether MSFS actually
// has focus (see main.cpp) gets both: best quality when MSFS is in the background, no
// contention when it's the focused/active render target. The recurrent state tensors are
// plain CPU-resident data fed as input regardless of which session runs, so a single
// EyeState pair is shared across both - "which device processed the last call" doesn't
// matter to the state itself.
class SegmentationEngine {
public:
    SegmentationEngine();
    ~SegmentationEngine();

    // Loads the ONNX model twice: a CPU-only session (always attempted, becomes the
    // guaranteed fallback) and a CUDA session (best-effort - failures here are caught and
    // logged, but don't fail the overall load as long as the CPU session succeeded).
    // Never throws. isReady() reflects the CPU session (the guaranteed baseline);
    // isGpuReady() reports whether the GPU session is also available.
    bool loadModel(const std::string& modelPath);

    bool isReady() const { return m_ready; }
    bool isGpuReady() const { return m_gpuSession != nullptr; }
    const std::string& getLastError() const { return m_lastError; }

    // Computes a single-channel (CV_8UC1) alpha matte for one eye's frame, resized to
    // match the input frame's own resolution. Returns an empty Mat on failure (caller
    // should treat that as "fall through to chroma-key for this frame"). preferGpu is
    // honored only if isGpuReady() - otherwise silently runs on CPU regardless.
    cv::Mat computeAlpha(const cv::Mat& bgrFrame, bool isLeftEye, bool preferGpu);

    // Clears both eyes' recurrent state back to zero. Call this whenever segmentation
    // mode is freshly (re-)entered, or the camera source changes - otherwise stale state
    // from an unrelated prior scene can bleed into the first few frames of a fresh session.
    void resetState();

    // Live-tunable preprocessing knobs (see computeAlpha) - exposed so quality can be
    // dialed in from the UI without a rebuild. Cheap to call every frame; only takes
    // effect on the next computeAlpha call.
    void setAutoGainEnabled(bool enabled) { m_autoGainEnabled = enabled; }
    void setAutoGainTarget(float target) { m_autoGainTarget = target; }
    void setAutoGainMaxGain(float maxGain) { m_autoGainMaxGain = maxGain; }
    void setClaheClipLimit(float clipLimit);
    bool isAutoGainEnabled() const { return m_autoGainEnabled; }
    float getAutoGainTarget() const { return m_autoGainTarget; }
    float getAutoGainMaxGain() const { return m_autoGainMaxGain; }
    float getClaheClipLimit() const { return m_claheClipLimit; }

private:
    struct EyeState {
        std::vector<Ort::Value> recurrent;  // r1i..r4i, replaced with r1o..r4o after each call
        bool initialized = false;
        // downsample_ratio used to produce the currently-stored recurrent tensors. RVM's
        // recurrent state is sized relative to the downsampled resolution, so reusing it
        // at a different ratio (e.g. after a GPU<->CPU session switch, which use different
        // ratios) breaks the model's internal shape assumptions. -1 = state is the generic
        // (1,1,1,1) zero seed, which is ratio-agnostic.
        float lastRatio = -1.0f;
    };

    std::unique_ptr<Ort::Env> m_env;
    std::unique_ptr<Ort::Session> m_cpuSession;
    std::unique_ptr<Ort::Session> m_gpuSession;  // null if CUDA EP init failed
    Ort::MemoryInfo m_memoryInfo{nullptr};

    std::vector<std::string> m_inputNames;
    std::vector<std::string> m_outputNames;
    // Index of "src" within m_inputNames, and of "downsample_ratio" - found by name at
    // load time rather than assumed, since export scripts/versions can rename things.
    int m_srcInputIndex = -1;
    int m_downsampleRatioIndex = -1;
    // Indices of r1i..r4i within m_inputNames, and r1o..r4o within m_outputNames/outputs,
    // in matching order (recurrentInputIndices[i] pairs with recurrentOutputIndices[i]).
    std::vector<int> m_recurrentInputIndices;
    std::vector<int> m_recurrentOutputIndices;
    int m_alphaOutputIndex = -1;  // index of "pha" within outputs

    // Higher = the model's internal low-res pass sees more detail = better confidence away
    // from frame center, but costs more compute. Now that GPU and CPU are independent
    // sessions, each gets its own value: full quality on GPU (used when MSFS isn't the
    // focused/contending window, so the extra cost is free), reduced on CPU (used while
    // MSFS IS focused, where the user's attention is on flying, not their hands, and
    // responsiveness matters more than edge quality).
    float m_downsampleRatioGpu = 0.5f;
    float m_downsampleRatioCpu = 0.33f;

    EyeState m_leftState;
    EyeState m_rightState;

    bool m_ready = false;
    std::string m_lastError;

    // Dark-background brightness compensation for the model's input only (never affects
    // the displayed frame). Off by default - it was tried as an always-on fix and made
    // quality worse in practice, so it's opt-in and tunable rather than a fixed guess.
    bool m_autoGainEnabled = false;
    float m_autoGainTarget = 140.0f;
    float m_autoGainMaxGain = 3.0f;
    float m_claheClipLimit = 2.0f;
    cv::Ptr<cv::CLAHE> m_clahe;

    void makeZeroState(EyeState& state);
};
