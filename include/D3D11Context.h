#pragma once

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <string>
#include <memory>

using Microsoft::WRL::ComPtr;

/**
 * @brief DirectX 11 rendering context wrapper
 *
 * Manages D3D11 device, context, swapchain, and resources.
 * Optimized for high-performance VR overlay rendering.
 */
class D3D11Context {
public:
    D3D11Context();
    ~D3D11Context();

    // Initialization
    bool initialize(HWND hwnd, int width, int height);
    void shutdown();

    // Device accessors
    ID3D11Device* getDevice() const { return m_device.Get(); }
    ID3D11DeviceContext* getContext() const { return m_context.Get(); }
    IDXGISwapChain* getSwapChain() const { return m_swapChain.Get(); }

    // Render target management
    ID3D11RenderTargetView* getBackBufferRTV() const { return m_backBufferRTV.Get(); }

    // Helper: Create a 2D texture for camera frames
    bool createTexture2D(int width, int height, DXGI_FORMAT format, ID3D11Texture2D** outTexture, ID3D11ShaderResourceView** outSRV);

    // Helper: Update texture from CPU data (e.g., cv::Mat)
    void updateTexture(ID3D11Texture2D* texture, const void* data, size_t pitch);

    // Rendering helpers
    void clearRenderTarget(ID3D11RenderTargetView* rtv, float r, float g, float b, float a);
    void present();

    bool isInitialized() const { return m_initialized; }

private:
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<IDXGISwapChain> m_swapChain;
    ComPtr<ID3D11RenderTargetView> m_backBufferRTV;

    bool m_initialized = false;

    bool createDeviceAndSwapChain(HWND hwnd, int width, int height);
    bool createBackBufferRTV();
};
