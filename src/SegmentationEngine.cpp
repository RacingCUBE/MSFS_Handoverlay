#include "SegmentationEngine.h"
#include <iostream>
#include <array>
#include <algorithm>
#include <chrono>

SegmentationEngine::SegmentationEngine() {
    m_clahe = cv::createCLAHE(m_claheClipLimit, cv::Size(8, 8));
}

SegmentationEngine::~SegmentationEngine() {
}

void SegmentationEngine::setClaheClipLimit(float clipLimit) {
    m_claheClipLimit = clipLimit;
    if (m_clahe) m_clahe->setClipLimit(clipLimit);
}

void SegmentationEngine::makeZeroState(EyeState& state) {
    state.recurrent.clear();
    for (int i = 0; i < 4; ++i) {
        // RVM's reference implementation seeds recurrent state as a minimal (1,1,1,1)
        // zero tensor - the model broadcasts it internally on first use.
        static const std::array<int64_t, 4> shape = {1, 1, 1, 1};
        static float zero = 0.0f;
        state.recurrent.push_back(Ort::Value::CreateTensor<float>(
            m_memoryInfo, &zero, 1, shape.data(), shape.size()));
    }
    state.initialized = true;
    state.lastRatio = -1.0f;
}

bool SegmentationEngine::loadModel(const std::string& modelPath) {
    m_ready = false;
    m_lastError.clear();

    try {
        m_env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "SegmentationEngine");
        std::wstring wideModelPath(modelPath.begin(), modelPath.end());

        // CPU session: the mandatory baseline. If this fails, there's nothing usable.
        Ort::SessionOptions cpuOptions;
        cpuOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        m_cpuSession = std::make_unique<Ort::Session>(*m_env, wideModelPath.c_str(), cpuOptions);

        m_memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

        Ort::AllocatorWithDefaultOptions allocator;

        m_inputNames.clear();
        size_t inputCount = m_cpuSession->GetInputCount();
        for (size_t i = 0; i < inputCount; ++i) {
            auto namePtr = m_cpuSession->GetInputNameAllocated(i, allocator);
            m_inputNames.push_back(std::string(namePtr.get()));
        }

        m_outputNames.clear();
        size_t outputCount = m_cpuSession->GetOutputCount();
        for (size_t i = 0; i < outputCount; ++i) {
            auto namePtr = m_cpuSession->GetOutputNameAllocated(i, allocator);
            m_outputNames.push_back(std::string(namePtr.get()));
        }

        // Resolve the fixed inputs/outputs by name rather than assuming a position -
        // export scripts/versions can reorder or rename these. Identical for both
        // sessions (same model file), so only needs doing once.
        m_srcInputIndex = -1;
        m_downsampleRatioIndex = -1;
        m_recurrentInputIndices.clear();
        for (int i = 0; i < static_cast<int>(m_inputNames.size()); ++i) {
            const std::string& name = m_inputNames[i];
            if (name == "src") m_srcInputIndex = i;
            else if (name == "downsample_ratio") m_downsampleRatioIndex = i;
            else if (name == "r1i" || name == "r2i" || name == "r3i" || name == "r4i") {
                m_recurrentInputIndices.push_back(i);
            }
        }
        // r1i..r4i must line up in order with r1o..r4o for state feedback next frame.
        std::sort(m_recurrentInputIndices.begin(), m_recurrentInputIndices.end(),
                  [this](int a, int b) { return m_inputNames[a] < m_inputNames[b]; });

        m_alphaOutputIndex = -1;
        m_recurrentOutputIndices.clear();
        for (int i = 0; i < static_cast<int>(m_outputNames.size()); ++i) {
            const std::string& name = m_outputNames[i];
            if (name == "pha") m_alphaOutputIndex = i;
            else if (name == "r1o" || name == "r2o" || name == "r3o" || name == "r4o") {
                m_recurrentOutputIndices.push_back(i);
            }
        }
        std::sort(m_recurrentOutputIndices.begin(), m_recurrentOutputIndices.end(),
                  [this](int a, int b) { return m_outputNames[a] < m_outputNames[b]; });

        if (m_srcInputIndex < 0 || m_alphaOutputIndex < 0 ||
            m_recurrentInputIndices.size() != 4 || m_recurrentOutputIndices.size() != 4) {
            m_lastError = "Model does not expose the expected RVM input/output tensor names "
                          "(src, r1i-r4i, downsample_ratio / pha, r1o-r4o)";
            std::cerr << "[SegmentationEngine] " << m_lastError << std::endl;
            return false;
        }

        resetState();

        // Warm up the CPU session - must succeed, this is the guaranteed baseline.
        cv::Mat warmup(480, 640, CV_8UC3, cv::Scalar(0, 0, 0));
        auto cpuStart = std::chrono::steady_clock::now();
        cv::Mat cpuResult = computeAlpha(warmup, true, /*preferGpu=*/false);
        auto cpuElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - cpuStart).count();
        resetState();

        if (cpuResult.empty()) {
            m_lastError = "Model loaded but CPU warm-up inference failed: " + m_lastError;
            std::cerr << "[SegmentationEngine] " << m_lastError << std::endl;
            return false;
        }
        std::cout << "[SegmentationEngine] CPU session ready, warm-up took " << cpuElapsedMs << "ms"
                  << std::endl;

        // GPU session: best-effort. A failure here (missing CUDA/cuDNN DLLs, driver issue,
        // etc.) is caught and logged but does NOT fail the overall load - the CPU session
        // above is already a fully working fallback. Kept in a separate try/catch so a GPU
        // problem can never take down the whole feature.
        try {
            Ort::SessionOptions gpuOptions;
            gpuOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
            OrtCUDAProviderOptions cudaOptions;  // default-constructed: device_id 0, sane defaults
            gpuOptions.AppendExecutionProvider_CUDA(cudaOptions);
            auto candidateGpuSession = std::make_unique<Ort::Session>(*m_env, wideModelPath.c_str(), gpuOptions);

            m_gpuSession = std::move(candidateGpuSession);
            auto gpuStart = std::chrono::steady_clock::now();
            cv::Mat gpuResult = computeAlpha(warmup, true, /*preferGpu=*/true);
            auto gpuElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - gpuStart).count();
            resetState();

            if (gpuResult.empty()) {
                std::cerr << "[SegmentationEngine] GPU warm-up inference failed, GPU switching unavailable: "
                          << m_lastError << std::endl;
                m_gpuSession.reset();
            } else {
                std::cout << "[SegmentationEngine] GPU session ready, warm-up took " << gpuElapsedMs << "ms"
                          << std::endl;
            }
        } catch (const Ort::Exception& e) {
            std::cerr << "[SegmentationEngine] GPU session unavailable, will run CPU-only: "
                      << e.what() << std::endl;
            m_gpuSession.reset();
        }

        m_lastError.clear();  // any transient GPU warm-up error shouldn't linger as "the" error
        m_ready = true;
        return true;
    } catch (const Ort::Exception& e) {
        m_lastError = std::string("onnxruntime error: ") + e.what();
        std::cerr << "[SegmentationEngine] " << m_lastError << std::endl;
        m_ready = false;
        return false;
    } catch (const std::exception& e) {
        m_lastError = std::string("error: ") + e.what();
        std::cerr << "[SegmentationEngine] " << m_lastError << std::endl;
        m_ready = false;
        return false;
    }
}

void SegmentationEngine::resetState() {
    if (!m_cpuSession) return;
    makeZeroState(m_leftState);
    makeZeroState(m_rightState);
}

cv::Mat SegmentationEngine::computeAlpha(const cv::Mat& bgrFrame, bool isLeftEye, bool preferGpu) {
    if ((!m_cpuSession && !m_gpuSession) || bgrFrame.empty()) {
        return cv::Mat();
    }
    Ort::Session* session = (preferGpu && m_gpuSession) ? m_gpuSession.get() : m_cpuSession.get();

    EyeState& state = isLeftEye ? m_leftState : m_rightState;
    if (!state.initialized) {
        makeZeroState(state);
    }

    // Use the session actually selected (not just the preferGpu request) in case GPU
    // was requested but isn't available and computeAlpha silently fell back to CPU.
    float activeRatio = (session == m_gpuSession.get()) ? m_downsampleRatioGpu : m_downsampleRatioCpu;

    // The recurrent state stored above was produced at a different downsample_ratio
    // (e.g. the GPU/CPU session just switched because MSFS's focus state changed) -
    // reusing it now would feed the model shapes it doesn't expect and throw. Drop back
    // to the ratio-agnostic zero seed instead; costs one frame of temporal smoothing,
    // not a permanent failure.
    if (state.lastRatio >= 0.0f && state.lastRatio != activeRatio) {
        makeZeroState(state);
    }

    try {
        const int width = bgrFrame.cols;
        const int height = bgrFrame.rows;

        // Normalize brightness/contrast before inference (not for display - only this
        // engine's input is affected). The rig's two physical cameras have different
        // exposure/contrast characteristics (already known from needing separate chroma-key
        // tuning per eye); the segmentation model loses confidence - and produces visibly
        // weaker alpha - on whichever eye happens to be darker/lower-contrast. CLAHE on the
        // L channel equalizes local contrast without color-shifting, so both eyes reach the
        // model with comparable effective quality regardless of per-camera exposure.
        cv::Mat normalized;
        {
            cv::Mat lab;
            cv::cvtColor(bgrFrame, lab, cv::COLOR_BGR2Lab);
            std::vector<cv::Mat> labChannels(3);
            cv::split(lab, labChannels);

            // Optional dark-background brightness lift, off by default (see header - it
            // regressed quality when tried as an always-on fix). Tunable from the UI for
            // anyone who wants to experiment with it for a specific dark-background setup.
            if (m_autoGainEnabled) {
                double meanL = cv::mean(labChannels[0])[0];
                if (meanL > 1.0 && meanL < m_autoGainTarget) {
                    double gain = std::min(static_cast<double>(m_autoGainMaxGain), m_autoGainTarget / meanL);
                    labChannels[0].convertTo(labChannels[0], -1, gain, 0.0);
                }
            }

            m_clahe->apply(labChannels[0], labChannels[0]);
            cv::merge(labChannels, lab);
            cv::cvtColor(lab, normalized, cv::COLOR_Lab2BGR);
        }

        // BGR8 -> RGB float32 [0,1], NCHW layout (1,3,H,W).
        cv::Mat rgb;
        cv::cvtColor(normalized, rgb, cv::COLOR_BGR2RGB);
        cv::Mat rgbFloat;
        rgb.convertTo(rgbFloat, CV_32FC3, 1.0 / 255.0);

        std::vector<float> chwData(static_cast<size_t>(3) * width * height);
        std::vector<cv::Mat> channels(3);
        for (int c = 0; c < 3; ++c) {
            channels[c] = cv::Mat(height, width, CV_32FC1,
                                   chwData.data() + static_cast<size_t>(c) * width * height);
        }
        cv::split(rgbFloat, channels);

        std::array<int64_t, 4> srcShape = {1, 3, height, width};
        Ort::Value srcTensor = Ort::Value::CreateTensor<float>(
            m_memoryInfo, chwData.data(), chwData.size(), srcShape.data(), srcShape.size());

        std::array<int64_t, 1> ratioShape = {1};
        Ort::Value ratioTensor = Ort::Value::CreateTensor<float>(
            m_memoryInfo, &activeRatio, 1, ratioShape.data(), ratioShape.size());

        // Assemble inputs in the model's own declared order.
        std::vector<Ort::Value> inputTensors;
        inputTensors.reserve(m_inputNames.size());
        for (size_t i = 0; i < m_inputNames.size(); ++i) {
            if (static_cast<int>(i) == m_srcInputIndex) {
                inputTensors.push_back(std::move(srcTensor));
            } else if (static_cast<int>(i) == m_downsampleRatioIndex) {
                inputTensors.push_back(std::move(ratioTensor));
            } else {
                auto it = std::find(m_recurrentInputIndices.begin(), m_recurrentInputIndices.end(),
                                     static_cast<int>(i));
                size_t slot = std::distance(m_recurrentInputIndices.begin(), it);
                inputTensors.push_back(std::move(state.recurrent[slot]));
            }
        }

        std::vector<const char*> inputNamesC;
        for (const auto& n : m_inputNames) inputNamesC.push_back(n.c_str());
        std::vector<const char*> outputNamesC;
        for (const auto& n : m_outputNames) outputNamesC.push_back(n.c_str());

        std::vector<Ort::Value> outputs = session->Run(
            Ort::RunOptions{nullptr},
            inputNamesC.data(), inputTensors.data(), inputTensors.size(),
            outputNamesC.data(), outputNamesC.size());

        // Store updated recurrent state for next call.
        for (size_t slot = 0; slot < m_recurrentOutputIndices.size(); ++slot) {
            state.recurrent[slot] = std::move(outputs[m_recurrentOutputIndices[slot]]);
        }
        state.lastRatio = activeRatio;

        // Extract the alpha matte.
        Ort::Value& alphaTensor = outputs[m_alphaOutputIndex];
        auto shapeInfo = alphaTensor.GetTensorTypeAndShapeInfo();
        auto shape = shapeInfo.GetShape();  // expected (1,1,H,W)
        int outH = static_cast<int>(shape[shape.size() - 2]);
        int outW = static_cast<int>(shape[shape.size() - 1]);
        const float* alphaData = alphaTensor.GetTensorData<float>();

        cv::Mat alphaFloat(outH, outW, CV_32FC1, const_cast<float*>(alphaData));
        cv::Mat alpha8;
        alphaFloat.convertTo(alpha8, CV_8UC1, 255.0);

        if (outW != width || outH != height) {
            cv::resize(alpha8, alpha8, cv::Size(width, height), 0, 0, cv::INTER_LINEAR);
        }
        return alpha8;
    } catch (const Ort::Exception& e) {
        m_lastError = std::string("inference error: ") + e.what();
        std::cerr << "[SegmentationEngine] " << m_lastError << std::endl;
        // The recurrent state slots were already moved out into inputTensors above (to
        // avoid copying Ort::Value), so on any failure they're left null - without this,
        // every future call would immediately fail too ("NULL input supplied for input
        // r1i"), turning one bad frame into a permanent lockup.
        makeZeroState(state);
        return cv::Mat();
    } catch (const std::exception& e) {
        m_lastError = std::string("inference error: ") + e.what();
        std::cerr << "[SegmentationEngine] " << m_lastError << std::endl;
        makeZeroState(state);
        return cv::Mat();
    }
}
