#include "SharedMemory.h"
#include <iostream>

namespace MSFSHandOverlay {
namespace IPC {

SharedFrameBuffer::SharedFrameBuffer() {
}

SharedFrameBuffer::~SharedFrameBuffer() {
    if (m_mappedMemory) {
        UnmapViewOfFile(m_mappedMemory);
        m_mappedMemory = nullptr;
    }
    if (m_fileMapping) {
        CloseHandle(m_fileMapping);
        m_fileMapping = nullptr;
    }
}

bool SharedFrameBuffer::createSharedMemory() {
    // Create file mapping (main app creates this)
    m_fileMapping = CreateFileMappingA(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0,
        static_cast<DWORD>(SHARED_MEMORY_SIZE),
        SHARED_MEMORY_NAME
    );

    if (!m_fileMapping) {
        std::cerr << "Failed to create file mapping: " << GetLastError() << std::endl;
        return false;
    }

    // Map view of file
    m_mappedMemory = MapViewOfFile(
        m_fileMapping,
        FILE_MAP_ALL_ACCESS,
        0,
        0,
        SHARED_MEMORY_SIZE
    );

    if (!m_mappedMemory) {
        std::cerr << "Failed to map view of file: " << GetLastError() << std::endl;
        CloseHandle(m_fileMapping);
        m_fileMapping = nullptr;
        return false;
    }

    // Initialize pointers
    m_header = static_cast<FrameHeader*>(m_mappedMemory);
    m_frameData = static_cast<char*>(m_mappedMemory) + sizeof(FrameHeader);

    // Initialize header
    ZeroMemory(m_header, sizeof(FrameHeader));
    m_header->width = 1280;
    m_header->height = 480;
    m_header->stride = 1280 * 4;
    m_header->format = 28; // DXGI_FORMAT_R8G8B8A8_UNORM
    m_header->frameReady = false;

    std::cout << "[IPC] Shared memory created successfully" << std::endl;
    return true;
}

bool SharedFrameBuffer::openSharedMemory() {
    // Open existing file mapping (API layer opens this)
    m_fileMapping = OpenFileMappingA(
        FILE_MAP_ALL_ACCESS,
        FALSE,
        SHARED_MEMORY_NAME
    );

    if (!m_fileMapping) {
        // Not an error - main app may not be running yet
        return false;
    }

    // Map view of file
    m_mappedMemory = MapViewOfFile(
        m_fileMapping,
        FILE_MAP_ALL_ACCESS,
        0,
        0,
        SHARED_MEMORY_SIZE
    );

    if (!m_mappedMemory) {
        std::cerr << "[API Layer] Failed to map view of file: " << GetLastError() << std::endl;
        CloseHandle(m_fileMapping);
        m_fileMapping = nullptr;
        return false;
    }

    // Initialize pointers
    m_header = static_cast<FrameHeader*>(m_mappedMemory);
    m_frameData = static_cast<char*>(m_mappedMemory) + sizeof(FrameHeader);

    std::cout << "[API Layer] Shared memory opened successfully" << std::endl;
    return true;
}

} // namespace IPC
} // namespace MSFSHandOverlay
