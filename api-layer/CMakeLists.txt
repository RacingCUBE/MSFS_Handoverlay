cmake_minimum_required(VERSION 3.15)
project(MSFSHandOverlay_Layer VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find OpenXR (headers only - do NOT link the loader library)
find_package(OpenXR CONFIG REQUIRED)

# DirectX 11
if(WIN32)
    set(DIRECTX_LIBRARIES d3d11.lib d3d12.lib dxgi.lib d3dcompiler.lib)
endif()

# Source files
set(LAYER_SOURCES
    src/ApiLayer.cpp
    src/SharedMemory.cpp
)

set(LAYER_HEADERS
    include/ApiLayer.h
    include/SharedMemory.h
)

# Include directories
include_directories(
    include
)

# Create DLL (with .def file for proper export - fixes __declspec(dllexport) + XRAPI_CALL issues)
# Use MODULE not SHARED - MODULE is for runtime-loaded plugins (like OpenXR layers)
add_library(${PROJECT_NAME} MODULE ${LAYER_SOURCES} ${LAYER_HEADERS} MSFSHandOverlay_Layer.def)

# Link libraries
# Use headers only - all OpenXR functions must be obtained via function pointers from the chain
target_link_libraries(${PROJECT_NAME}
    OpenXR::headers
    ${DIRECTX_LIBRARIES}
)

# Set output name
set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "MSFSHandOverlay_Layer"
    PREFIX ""
)

# Copy manifest JSON to output directory
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
    "${CMAKE_CURRENT_SOURCE_DIR}/MSFSHandOverlay_api_layer.json"
    $<TARGET_FILE_DIR:${PROJECT_NAME}>/MSFSHandOverlay_api_layer.json
)

# Install to OpenXR API layers directory
# Note: Requires admin privileges to write to ProgramData
# MODULE libraries need LIBRARY destination (not RUNTIME which is for SHARED libs)
install(TARGETS ${PROJECT_NAME}
    LIBRARY DESTINATION "$ENV{ProgramData}/OpenXR/1/api_layers/explicit.d"
)

install(FILES MSFSHandOverlay_api_layer.json
    DESTINATION "$ENV{ProgramData}/OpenXR/1/api_layers/explicit.d"
)

# Minimal test layer (for debugging)
add_library(MinimalTestLayer MODULE src/ApiLayerMinimal.cpp)
# Use headers only, not the loader library
target_link_libraries(MinimalTestLayer OpenXR::headers)
set_target_properties(MinimalTestLayer PROPERTIES
    OUTPUT_NAME "MSFSHandOverlay_Layer_Minimal"
    PREFIX ""
)
