#include "D3D11Context.h"
#include <iostream>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

D3D11Context::D3D11Context() {
}

D3D11Context::~D3D11Context() {
    shutdown();
}

bool D3D11Context::initialize(HWND hwnd, int width, int height) {
    if (m_initialized) {
        std::cerr << "D3D11Context already initialized" << std::endl;
        return false;
    }

    if (!createDeviceAndSwapChain(hwnd, width, height)) {
        std::cerr << "Failed to create D3D11 device and swap chain" << std::endl;
        return false;
    }

    if (!createBackBufferRTV()) {
        std::cerr << "Failed to create back buffer RTV" << std::endl;
        return false;
    }

    m_initialized = true;
    std::cout << "DirectX 11 initialized successfully" << std::endl;
    return true;
}

void D3D11Context::shutdown() {
    if (!m_initialized) return;

    // Release COM objects (ComPtr handles this automatically on destruction)
    m_backBufferRTV.Reset();
    m_swapChain.Reset();
    m_context.Reset();
    m_device.Reset();

    m_initialized = false;
    std::cout << "DirectX 11 shutdown complete" << std::endl;
}

bool D3D11Context::createDeviceAndSwapChain(HWND hwnd, int width, int height) {
    // Swap chain descriptor
    DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
    swapChainDesc.BufferCount = 2;  // Double buffering
    swapChainDesc.BufferDesc.Width = width;
    swapChainDesc.BufferDesc.Height = height;
    swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferDesc.RefreshRate.Numerator = 60;
    swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.OutputWindow = hwnd;
    swapChainDesc.SampleDesc.Count = 1;  // No MSAA
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.Windowed = TRUE;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;  // Modern flip model

    // Feature levels to try
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };

    D3D_FEATURE_LEVEL featureLevel;
    UINT createDeviceFlags = 0;

#ifdef _DEBUG
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    // Create device and swap chain
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,                    // Use default adapter
        D3D_DRIVER_TYPE_HARDWARE,   // Hardware acceleration
        nullptr,                    // No software rasterizer
        createDeviceFlags,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        &swapChainDesc,
        m_swapChain.GetAddressOf(),
        m_device.GetAddressOf(),
        &featureLevel,
        m_context.GetAddressOf()
    );

    if (FAILED(hr)) {
        std::cerr << "D3D11CreateDeviceAndSwapChain failed with HRESULT: 0x"
                  << std::hex << hr << std::dec << std::endl;
        return false;
    }

    std::cout << "DirectX 11 device created (Feature Level: "
              << ((featureLevel == D3D_FEATURE_LEVEL_11_1) ? "11.1" :
                  (featureLevel == D3D_FEATURE_LEVEL_11_0) ? "11.0" :
                  (featureLevel == D3D_FEATURE_LEVEL_10_1) ? "10.1" : "10.0")
              << ")" << std::endl;

    return true;
}

bool D3D11Context::createBackBufferRTV() {
    // Get back buffer texture from swap chain
    ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()));
    if (FAILED(hr)) {
        std::cerr << "Failed to get back buffer from swap chain" << std::endl;
        return false;
    }

    // Create render target view
    hr = m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, m_backBufferRTV.GetAddressOf());
    if (FAILED(hr)) {
        std::cerr << "Failed to create render target view" << std::endl;
        return false;
    }

    return true;
}

bool D3D11Context::createTexture2D(int width, int height, DXGI_FORMAT format,
                                   ID3D11Texture2D** outTexture, ID3D11ShaderResourceView** outSRV) {
    // Texture descriptor
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = format;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Usage = D3D11_USAGE_DYNAMIC;  // CPU write, GPU read
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    texDesc.MiscFlags = 0;

    HRESULT hr = m_device->CreateTexture2D(&texDesc, nullptr, outTexture);
    if (FAILED(hr)) {
        std::cerr << "Failed to create 2D texture" << std::endl;
        return false;
    }

    // Create shader resource view
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;

    hr = m_device->CreateShaderResourceView(*outTexture, &srvDesc, outSRV);
    if (FAILED(hr)) {
        std::cerr << "Failed to create shader resource view" << std::endl;
        (*outTexture)->Release();
        *outTexture = nullptr;
        return false;
    }

    return true;
}

void D3D11Context::updateTexture(ID3D11Texture2D* texture, const void* data, size_t pitch) {
    if (!texture || !data) return;

    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = m_context->Map(texture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (SUCCEEDED(hr)) {
        // Get texture dimensions
        D3D11_TEXTURE2D_DESC desc;
        texture->GetDesc(&desc);

        // Copy row by row (handle pitch differences)
        const uint8_t* srcData = static_cast<const uint8_t*>(data);
        uint8_t* dstData = static_cast<uint8_t*>(mapped.pData);

        for (UINT y = 0; y < desc.Height; ++y) {
            memcpy(dstData, srcData, pitch);
            srcData += pitch;
            dstData += mapped.RowPitch;
        }

        m_context->Unmap(texture, 0);
    }
}

void D3D11Context::clearRenderTarget(ID3D11RenderTargetView* rtv, float r, float g, float b, float a) {
    float clearColor[4] = { r, g, b, a };
    m_context->ClearRenderTargetView(rtv, clearColor);
}

void D3D11Context::present() {
    if (m_swapChain) {
        m_swapChain->Present(1, 0);  // VSync enabled (1), no flags
    }
}
