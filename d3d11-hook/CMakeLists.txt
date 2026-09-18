cmake_minimum_required(VERSION 3.15)
project(MSFS_D3D11_Hook VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Source files
set(HOOK_SOURCES
    src/D3D11Hook.cpp
    src/OverlayRenderer.cpp
)

set(HOOK_HEADERS
    include/D3D11Hook.h
    include/OverlayRenderer.h
)

include_directories(include)

# Create DLL
add_library(${PROJECT_NAME} SHARED ${HOOK_SOURCES} ${HOOK_HEADERS})

# Link D3D11
target_link_libraries(${PROJECT_NAME}
    d3d11.lib
    dxgi.lib
)

# Output name
set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "d3d11"
    PREFIX ""
)

# Install to MSFS directory (user will copy manually)
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION "${CMAKE_BINARY_DIR}/install"
)
