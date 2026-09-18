#pragma once

#define XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D12
#include <d3d11.h>
#include <d3d11on12.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openxr/openxr_loader_negotiation.h>
#include <memory>
#include <vector>
#include <unordered_map>

#include "SharedMemory.h"

namespace MSFSHandOverlay {
namespace APILayer {

// Vertex for overlay quad
struct OverlayVertex {
    float x, y, z;
    float u, v;
};

// D3D11 render pipeline for overlay compositing
struct RenderPipeline {
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11InputLayout* inputLayout = nullptr;
    ID3D11Buffer* vertexBuffer = nullptr;
    ID3D11Buffer* constantBuffer = nullptr;
    ID3D11BlendState* blendState = nullptr;
    ID3D11SamplerState* samplerState = nullptr;
    ID3D11RasterizerState* rasterizerState = nullptr;
    ID3D11DepthStencilState* depthStencilState = nullptr;
    ID3D11Texture2D* cameraTexture = nullptr;
    ID3D11ShaderResourceView* cameraSRV = nullptr;
    uint32_t cameraTexWidth = 0;
    uint32_t cameraTexHeight = 0;
    bool initialized = false;

    void release() {
        if (vs) { vs->Release(); vs = nullptr; }
        if (ps) { ps->Release(); ps = nullptr; }
        if (inputLayout) { inputLayout->Release(); inputLayout = nullptr; }
        if (vertexBuffer) { vertexBuffer->Release(); vertexBuffer = nullptr; }
        if (constantBuffer) { constantBuffer->Release(); constantBuffer = nullptr; }
        if (blendState) { blendState->Release(); blendState = nullptr; }
        if (samplerState) { samplerState->Release(); samplerState = nullptr; }
        if (rasterizerState) { rasterizerState->Release(); rasterizerState = nullptr; }
        if (depthStencilState) { depthStencilState->Release(); depthStencilState = nullptr; }
        if (cameraSRV) { cameraSRV->Release(); cameraSRV = nullptr; }
        if (cameraTexture) { cameraTexture->Release(); cameraTexture = nullptr; }
        initialized = false;
    }
};

// OpenXR API layer implementation
class HandOverlayLayer {
public:
    HandOverlayLayer();
    ~HandOverlayLayer();

    // OpenXR API function interception
    XrResult xrCreateInstance(const XrInstanceCreateInfo* createInfo, XrInstance* instance);
    XrResult xrGetInstanceProcAddr(XrInstance instance, const char* name, PFN_xrVoidFunction* function);
    XrResult xrCreateSession(XrInstance instance, const XrSessionCreateInfo* createInfo, XrSession* session);
    XrResult xrDestroySession(XrSession session);
    XrResult xrEndFrame(XrSession session, const XrFrameEndInfo* frameEndInfo);
    XrResult xrCreateSwapchain(XrSession session, const XrSwapchainCreateInfo* createInfo, XrSwapchain* swapchain);
    XrResult xrDestroySwapchain(XrSwapchain swapchain);
    XrResult xrCreateReferenceSpace(XrSession session, const XrReferenceSpaceCreateInfo* createInfo, XrSpace* space);
    XrResult xrAcquireSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageAcquireInfo* acquireInfo, uint32_t* index);
    XrResult xrLocateViews(XrSession session, const XrViewLocateInfo* viewLocateInfo,
        XrViewState* viewState, uint32_t viewCapacityInput,
        uint32_t* viewCountOutput, XrView* views);

    // Store next layer functions
    void setNextGetInstanceProcAddr(PFN_xrGetInstanceProcAddr next) { m_nextGetInstanceProcAddr = next; }

private:
    // Session state
    struct SessionData {
        XrSession session;
        ID3D11Device* d3dDevice = nullptr;
        ID3D11DeviceContext* d3dContext = nullptr;
        // D3D12 support via D3D11On12
        bool isD3D12 = false;
        ID3D12Device* d3d12Device = nullptr;
        ID3D12CommandQueue* d3d12Queue = nullptr;
        ID3D11On12Device* d3d11on12 = nullptr;
        uint32_t lastFrameSequence = 0;
        uint32_t staleFrameCount = 0;
        RenderPipeline pipeline;
    };

    std::unordered_map<XrSession, std::unique_ptr<SessionData>> m_sessions;

    // Track app swapchain D3D11 textures and acquired image indices
    std::unordered_map<XrSwapchain, std::vector<ID3D11Texture2D*>> m_appSwapchainTextures;
    // Track wrapped D3D12 resources for acquire/release
    std::unordered_map<XrSwapchain, std::vector<ID3D11Resource*>> m_wrappedD3D12Resources;
    std::unordered_map<XrSwapchain, uint32_t> m_appAcquiredIndex;

    // Shared memory for frame data
    std::unique_ptr<IPC::SharedFrameBuffer> m_sharedFrames;

    // Next layer function pointers
    PFN_xrGetInstanceProcAddr m_nextGetInstanceProcAddr = nullptr;
    PFN_xrCreateSession m_nextCreateSession = nullptr;
    PFN_xrDestroySession m_nextDestroySession = nullptr;
    PFN_xrEndFrame m_nextEndFrame = nullptr;
    PFN_xrCreateSwapchain m_nextCreateSwapchain = nullptr;
    PFN_xrDestroySwapchain m_nextDestroySwapchain = nullptr;
    PFN_xrCreateReferenceSpace m_nextCreateReferenceSpace = nullptr;
    PFN_xrAcquireSwapchainImage m_nextAcquireSwapchainImage = nullptr;
    PFN_xrEnumerateSwapchainImages m_nextEnumerateSwapchainImages = nullptr;
    PFN_xrLocateViews m_nextLocateViews = nullptr;

    // The overlay sits at a fixed world-space position (config posX/Y/Z) and gets its
    // parallax entirely from being rendered with the eye's real world-space pose each
    // frame - a fixed object viewed from a moving camera naturally parallaxes on its own,
    // no per-frame position hack needed. That pose must come from a space we know is
    // world-anchored: proj->views[eye].pose (from the app's xrEndFrame submission) never
    // states which XrSpace it's expressed in, and can diverge from raw head tracking if
    // the app applies its own camera effects (seat offset, smoothing) before submitting.
    // xrLocateViews is called by the app itself, every frame, to get the poses it renders
    // its own world with - so poses captured there are the trustworthy source, PROVIDED
    // they were queried against the same XrSpace the app's projection layer is itself
    // expressed in (XrCompositionLayerProjection::space). Confirmed by logging that MSFS
    // calls xrLocateViews against MULTIPLE different XrSpace handles over a session (at
    // least one large-magnitude "stage-ish" space and one small-magnitude "recentered
    // local" space seen in practice) - blindly using whichever call happened most recently
    // silently mixed poses from unrelated coordinate frames, which is what actually broke
    // sideways-translation parallax while rotation still looked fine (rotation error from
    // a coordinate-origin mismatch is far less visually obvious than a position error).
    // Only trust the captured pose when m_locatedViewSpace == proj->space; otherwise fall
    // back to proj's own pose. (Replaces an earlier "slew compensation" hack that tried to
    // patch proj's pose after the fact by diffing it against this same data; that fought
    // the natural parallax instead of fixing the actual mismatch, making the overlay look
    // head-locked instead of anchored in the cockpit.)
    XrPosef m_locatedViewPose[2] = {};
    XrSpace m_locatedViewSpace = XR_NULL_HANDLE;
    uint32_t m_locatedViewCount = 0;
    bool m_hasLocatedViews = false;

    // Per-eye smoothed orientation (slerped toward the real tracked orientation each frame)
    // used for the view matrix instead of the raw value, to take the edge off small-scale
    // tracking jitter during head rotation. Position is deliberately NOT smoothed here -
    // see the comment at its use site in renderOntoProjection.
    DirectX::XMVECTOR m_smoothedOrientation[2] = {};
    bool m_hasSmoothedOrientation[2] = { false, false };

    // Helper functions
    bool initRenderPipeline(SessionData* sd);
    void ensureCameraTexture(SessionData* sd, uint32_t width, uint32_t height, uint32_t format);
    bool renderOntoProjection(SessionData* sd, const XrFrameEndInfo* frameEndInfo);
    static DXGI_FORMAT getRTVFormat(DXGI_FORMAT texFormat);
};

// Global layer instance
extern std::unique_ptr<HandOverlayLayer> g_layer;

} // namespace APILayer
} // namespace MSFSHandOverlay

// OpenXR API layer entry points (exported from DLL)
extern "C" {

__declspec(dllexport) XRAPI_ATTR XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(
    const XrNegotiateLoaderInfo* loaderInfo,
    const char* layerName,
    XrNegotiateApiLayerRequest* apiLayerRequest);

}
