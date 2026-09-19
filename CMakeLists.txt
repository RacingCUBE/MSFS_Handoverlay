cmake_minimum_required(VERSION 3.15)
project(MSFSHandOverlay VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Find packages
find_package(OpenCV REQUIRED)
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(glfw3 REQUIRED)
find_package(OpenXR CONFIG REQUIRED)

# DirectX 11 (Windows SDK - no find_package needed, libraries are standard)
if(WIN32)
    set(DIRECTX_LIBRARIES d3d11.lib dxgi.lib d3dcompiler.lib)
endif()

# ImGui
set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/libs/imgui")

# OpenVR (used for Valve Index built-in tracked camera access)
set(OPENVR_DIR "${CMAKE_SOURCE_DIR}/libs/openvr/openvr-master")
set(OPENVR_INCLUDE_DIR "${OPENVR_DIR}/headers")
set(OPENVR_LIB "${OPENVR_DIR}/lib/win64/openvr_api.lib")

# ONNX Runtime (GPU/CUDA build) - official prebuilt binary from Microsoft's GitHub release,
# not vcpkg: vcpkg's onnxruntime[cuda] port fails to build against the protobuf version
# vcpkg currently ships (protobuf 6.33.4 vs. onnxruntime's older expected API surface -
# "variable cannot have incomplete type ...EnumTraitsImpl::Undefined" in message_lite.h).
# The prebuilt binary is self-contained and sidesteps that build-time conflict entirely.
set(ONNXRUNTIME_DIR "${CMAKE_SOURCE_DIR}/third_party/onnxruntime-win-x64-gpu-1.23.2")
set(ONNXRUNTIME_INCLUDE_DIR "${ONNXRUNTIME_DIR}/include")
set(ONNXRUNTIME_LIB "${ONNXRUNTIME_DIR}/lib/onnxruntime.lib")

# SimConnect (MSFS SDK) - fires client events (e.g. autopilot knob inc/dec) into a
# running MSFS instance. Not vendored in this repo (it's part of the official, several-GB
# MSFS SDK installer, not something to check into source control) - expected at the
# standard install path. Using the static lib deliberately: it links the SimConnect
# client code directly into this exe, so no SimConnect.dll needs to be copied/shipped
# alongside it (unlike the dynamic lib at SimConnect SDK/lib/SimConnect.lib).
set(SIMCONNECT_SDK_DIR "C:/MSFS SDK/SimConnect SDK")
set(SIMCONNECT_INCLUDE_DIR "${SIMCONNECT_SDK_DIR}/include")
set(SIMCONNECT_LIB "${SIMCONNECT_SDK_DIR}/lib/static/SimConnect.lib")
if(NOT EXISTS "${SIMCONNECT_LIB}")
    message(FATAL_ERROR "SimConnect SDK not found at ${SIMCONNECT_SDK_DIR} - install the "
        "MSFS SDK (in-sim: General Options > Developer Mode > enable, then install the SDK "
        "from the Developer menu) or update SIMCONNECT_SDK_DIR above to match your install.")
endif()

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/api-layer/include
    ${OpenCV_INCLUDE_DIRS}
    ${OPENVR_INCLUDE_DIR}
    ${ONNXRUNTIME_INCLUDE_DIR}
    ${SIMCONNECT_INCLUDE_DIR}
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

# Link directories
link_directories(
    ${OpenCV_LIB_DIR}
)

# ImGui source files (OpenGL3 + GLFW backend for hybrid mode)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

# Source files
set(SOURCES
    src/main.cpp
    src/VROverlay.cpp
    src/CameraCapture.cpp
    src/ShaderProgram.cpp
    src/Config.cpp
    src/D3D11Context.cpp
    src/HLSLShader.cpp
    src/SegmentationEngine.cpp
    src/TouchInput.cpp
    src/HandTouchTracker.cpp
    src/SimConnectClient.cpp
    api-layer/src/SharedMemory.cpp
    ${IMGUI_SOURCES}
)

set(HEADERS
    include/VROverlay.h
    include/CameraCapture.h
    include/ShaderProgram.h
    include/Config.h
    include/FrameData.h
    include/TripleBuffer.h
    include/PerformanceStats.h
    include/D3D11Context.h
    include/HLSLShader.h
    include/SegmentationEngine.h
    include/TouchInput.h
    include/HandTouchTracker.h
    include/SimConnectClient.h
)

# Executable (WIN32 = no console window)
add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${HEADERS})

# Set entry point to main() instead of WinMain()
if(MSVC)
    set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "/ENTRY:mainCRTStartup")
endif()

# Link libraries (Hybrid: OpenGL for window, DirectX + OpenXR for VR)
target_link_libraries(${PROJECT_NAME}
    ${OpenCV_LIBS}
    ${OPENGL_LIBRARIES}
    GLEW::GLEW
    glfw
    ${DIRECTX_LIBRARIES}
    OpenXR::openxr_loader
    ${OPENVR_LIB}
    ${ONNXRUNTIME_LIB}
    ${SIMCONNECT_LIB}
    ws2_32.lib     # SimConnect's static lib uses Winsock internally (TCP transport) - not pulled in automatically
    Shlwapi.lib    # ...and PathCombineA from Shlwapi
)

# Copy all required DLLs using dedicated batch script (PERMANENT FIX)
if(WIN32)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_SOURCE_DIR}/copy_runtime_dlls.bat $<TARGET_FILE_DIR:${PROJECT_NAME}>
        COMMENT "Copying runtime DLLs (permanent solution)..."
    )
endif()

# Copy shader files
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/shaders"
    $<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders
)

# Copy ONNX Runtime GPU DLLs (not covered by copy_runtime_dlls.bat's vcpkg-only wildcard,
# since this is a separately-sourced prebuilt binary, not a vcpkg package)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ONNXRUNTIME_DIR}/lib/onnxruntime.dll"
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/onnxruntime.dll
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ONNXRUNTIME_DIR}/lib/onnxruntime_providers_shared.dll"
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/onnxruntime_providers_shared.dll
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ONNXRUNTIME_DIR}/lib/onnxruntime_providers_cuda.dll"
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/onnxruntime_providers_cuda.dll
)

# Copy the specific CUDA/cuDNN runtime DLLs onnxruntime's CUDA execution provider actually
# needs (cudart/cublas/cublasLt + cuDNN's dispatcher and engine DLLs). Deliberately NOT the
# full CUDA Toolkit bin/ (cufft, curand, cusolver, npp*, nvrtc etc. total several GB and
# aren't used by this model) - copied rather than relying on system PATH, since a PATH
# change made after this process tree started won't be picked up without a fresh login.
set(CUDA_BIN_DIR "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.6/bin")
set(CUDA_RUNTIME_DLLS
    cudart64_12.dll
    cublas64_12.dll
    cublasLt64_12.dll
    cufft64_11.dll
    cudnn64_9.dll
    cudnn_ops64_9.dll
    cudnn_cnn64_9.dll
    cudnn_graph64_9.dll
    cudnn_heuristic64_9.dll
    cudnn_adv64_9.dll
    cudnn_ext64_9.dll
    cudnn_engines_tensor_ir64_9.dll
    cudnn_engines_precompiled64_9.dll
    cudnn_engines_runtime_compiled64_9.dll
)
foreach(_dll ${CUDA_RUNTIME_DLLS})
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CUDA_BIN_DIR}/${_dll}"
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/${_dll}
    )
endforeach()

# Copy the segmentation model (static shipped content, always overwrite - unlike config/,
# this isn't user-tuned state)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/models"
    $<TARGET_FILE_DIR:${PROJECT_NAME}>/models
)

# Copy config files only if settings.ini doesn't exist yet (preserve user settings across rebuilds)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${PROJECT_NAME}>/config
    COMMAND ${CMAKE_COMMAND}
        -Dsrc=${CMAKE_SOURCE_DIR}/config/settings.ini
        -Ddst=$<TARGET_FILE_DIR:${PROJECT_NAME}>/config/settings.ini
        -P ${CMAKE_SOURCE_DIR}/copy_if_missing.cmake
)

# Build OpenXR API Layer
add_subdirectory(api-layer)
