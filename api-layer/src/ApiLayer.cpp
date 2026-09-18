#include "ApiLayer.h"
#include <cmath>
#include <cstring>
#include <vector>
#include <windows.h>

using namespace DirectX;

// ── Embedded HLSL shaders ────────────────────────────────────────────────────

static const char* OVERLAY_VS = R"(
cbuffer Constants : register(b0) {
    row_major float4x4 viewProj;
    float4 uvRect;
    float4 chromaHSV;   // x=hueCenter/360, y=hueRange/360, z=satMin, w=valMin
    float4 chromaExtra; // x=edgeSoftness, y=borderMaskSize, z=borderMaskSoftness
    float4 extraParams; // x=handBrightness, y=alphaMode (unused by VS, see OVERLAY_PS)
};
struct VS_OUT {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};
VS_OUT main(float3 pos : POSITION, float2 uv : TEXCOORD0) {
    VS_OUT o;
    o.pos = mul(float4(pos, 1.0), viewProj);
    o.uv  = float2(lerp(uvRect.x, uvRect.z, uv.x),
                    lerp(uvRect.y, uvRect.w, uv.y));
    return o;
}
)";

static const char* OVERLAY_PS = R"(
Texture2D    camTex  : register(t0);
SamplerState camSamp : register(s0);
cbuffer Constants : register(b0) {
    row_major float4x4 viewProj;
    float4 uvRect;
    float4 chromaHSV;   // x=hueCenter, y=hueRange, z=satMin, w=valMin (hue in degrees)
    float4 chromaExtra; // x=edgeSoftness, y=borderMaskSize, z=borderMaskSoftness, w=inverseMode
    float4 extraParams; // x=handBrightness, y=alphaMode (0=chroma-key shader math, 1=alpha already in texture's own alpha channel from ML segmentation)
};

float3 rgb2hsv(float3 rgb) {
    float maxVal = max(max(rgb.r, rgb.g), rgb.b);
    float minVal = min(min(rgb.r, rgb.g), rgb.b);
    float delta = maxVal - minVal;

    float3 hsv;

    if (delta < 0.00001) {
        hsv.x = 0.0;
    } else if (maxVal == rgb.r) {
        hsv.x = 60.0 * fmod((rgb.g - rgb.b) / delta, 6.0);
    } else if (maxVal == rgb.g) {
        hsv.x = 60.0 * ((rgb.b - rgb.r) / delta + 2.0);
    } else {
        hsv.x = 60.0 * ((rgb.r - rgb.g) / delta + 4.0);
    }

    if (hsv.x < 0.0) hsv.x += 360.0;
    hsv.y = (maxVal < 0.00001) ? 0.0 : (delta / maxVal);
    hsv.z = maxVal;
    return hsv;
}

float hueDistance(float h1, float h2) {
    float diff = abs(h1 - h2);
    if (diff > 180.0) diff = 360.0 - diff;
    return diff;
}

float borderMask(float2 uv2, float maskSize, float softness) {
    if (maskSize < 0.001) return 1.0;
    float2 dist = min(uv2, 1.0 - uv2);
    float minDist = min(dist.x, dist.y);
    return smoothstep(maskSize - softness, maskSize, minDist);
}

float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET {
    float4 c = camTex.Sample(camSamp, uv);
    float alpha;

    if (extraParams.y > 0.5) {
        // ML segmentation mode: the companion app already computed a real per-pixel
        // alpha matte and baked it into the texture's own alpha channel - use it as-is
        // instead of re-deriving alpha from chroma-key math.
        alpha = c.a;
    } else {
        float3 hsv = rgb2hsv(c.rgb);

        float hueCenter    = chromaHSV.x;
        float hueRange     = chromaHSV.y;
        float satMin       = chromaHSV.z;
        float valMin       = chromaHSV.w;
        float edgeSoftness = chromaExtra.x;

        float hueDiff = hueDistance(hsv.x, hueCenter);

        // Scale edgeSoftness by 180 for hue (matches archive chromakey_fragment.glsl)
        float hueSoft = edgeSoftness * 180.0;
        float hueMatch = smoothstep(hueRange + hueSoft, hueRange - hueSoft, hueDiff);
        float satMatch = smoothstep(satMin - edgeSoftness, satMin + edgeSoftness, hsv.y);
        float valMatch = smoothstep(valMin - edgeSoftness, valMin + edgeSoftness, hsv.z);

        float keyAmount = hueMatch * satMatch * valMatch;

        if (chromaExtra.w > 0.5) {
            alpha = keyAmount;
        } else {
            alpha = 1.0 - keyAmount;
        }
    }

    // Border mask - applied identically in both modes
    float eyeU = (uv.x - uvRect.x) / (uvRect.z - uvRect.x);
    float eyeV = uv.y;
    float border = borderMask(float2(eyeU, eyeV), chromaExtra.y, chromaExtra.z);
    alpha *= border;

    alpha = saturate(alpha);
    float brightness = extraParams.x;
    return float4(c.rgb * brightness * alpha, alpha);
}
)";

// ── Logging helper ───────────────────────────────────────────────────────────

static void logMsg(const char* file, const char* msg) {
    HANDLE h = CreateFileA(file, FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD w;
        WriteFile(h, msg, (DWORD)strlen(msg), &w, NULL);
        CloseHandle(h);
    }
}

static void logFmt(const char* file, const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    logMsg(file, buf);
}

// ── DllMain ──────────────────────────────────────────────────────────────────

BOOL WINAPI DllMain(HINSTANCE, DWORD fdwReason, LPVOID) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        char path[MAX_PATH];
        GetModuleFileNameA(NULL, path, MAX_PATH);
        logFmt("C:\\Temp\\MSFSHandOverlay_DllMain.log",
            "=== DLL LOADED === Process: %s\r\n", path);
    }
    return TRUE;
}

namespace MSFSHandOverlay {
namespace APILayer {

std::unique_ptr<HandOverlayLayer> g_layer;

// ── Hook forward declarations ────────────────────────────────────────────────

static XrResult XRAPI_CALL Hook_xrCreateSession(XrInstance, const XrSessionCreateInfo*, XrSession*);
static XrResult XRAPI_CALL Hook_xrDestroySession(XrSession);
static XrResult XRAPI_CALL Hook_xrEndFrame(XrSession, const XrFrameEndInfo*);
static XrResult XRAPI_CALL Hook_xrCreateSwapchain(XrSession, const XrSwapchainCreateInfo*, XrSwapchain*);
static XrResult XRAPI_CALL Hook_xrDestroySwapchain(XrSwapchain);
static XrResult XRAPI_CALL Hook_xrCreateReferenceSpace(XrSession, const XrReferenceSpaceCreateInfo*, XrSpace*);
static XrResult XRAPI_CALL Hook_xrAcquireSwapchainImage(XrSwapchain, const XrSwapchainImageAcquireInfo*, uint32_t*);
static XrResult XRAPI_CALL Hook_xrLocateViews(XrSession, const XrViewLocateInfo*, XrViewState*, uint32_t, uint32_t*, XrView*);

// ── Constructor / Destructor ─────────────────────────────────────────────────

HandOverlayLayer::HandOverlayLayer() {
    logMsg("C:\\Temp\\MSFSHandOverlay_APILayer.log",
        "\n=== API Layer Init (Shader Pipeline) ===\n");

    m_sharedFrames = std::make_unique<IPC::SharedFrameBuffer>();
    if (m_sharedFrames->openSharedMemory())
        logMsg("C:\\Temp\\MSFSHandOverlay_APILayer.log", "[OK] Shared memory opened\n");
    else
        logMsg("C:\\Temp\\MSFSHandOverlay_APILayer.log", "[WARN] Shared memory not available\n");
}

HandOverlayLayer::~HandOverlayLayer() {}

// ── xrGetInstanceProcAddr ────────────────────────────────────────────────────

XrResult HandOverlayLayer::xrGetInstanceProcAddr(XrInstance instance, const char* name, PFN_xrVoidFunction* function) {
    if (!m_nextGetInstanceProcAddr) return XR_ERROR_HANDLE_INVALID;

    struct HookEntry { const char* name; PFN_xrVoidFunction hook; PFN_xrVoidFunction* stored; };
    HookEntry hooks[] = {
        {"xrCreateSession",        (PFN_xrVoidFunction)Hook_xrCreateSession,        (PFN_xrVoidFunction*)&m_nextCreateSession},
        {"xrDestroySession",       (PFN_xrVoidFunction)Hook_xrDestroySession,       (PFN_xrVoidFunction*)&m_nextDestroySession},
        {"xrEndFrame",             (PFN_xrVoidFunction)Hook_xrEndFrame,             (PFN_xrVoidFunction*)&m_nextEndFrame},
        {"xrCreateSwapchain",      (PFN_xrVoidFunction)Hook_xrCreateSwapchain,      (PFN_xrVoidFunction*)&m_nextCreateSwapchain},
        {"xrDestroySwapchain",     (PFN_xrVoidFunction)Hook_xrDestroySwapchain,     (PFN_xrVoidFunction*)&m_nextDestroySwapchain},
        {"xrCreateReferenceSpace", (PFN_xrVoidFunction)Hook_xrCreateReferenceSpace, (PFN_xrVoidFunction*)&m_nextCreateReferenceSpace},
        {"xrAcquireSwapchainImage",(PFN_xrVoidFunction)Hook_xrAcquireSwapchainImage,(PFN_xrVoidFunction*)&m_nextAcquireSwapchainImage},
        {"xrLocateViews",          (PFN_xrVoidFunction)Hook_xrLocateViews,          (PFN_xrVoidFunction*)&m_nextLocateViews},
    };

    for (auto& h : hooks) {
        if (strcmp(name, h.name) == 0) {
            if (!*h.stored) {
                PFN_xrVoidFunction next = nullptr;
                m_nextGetInstanceProcAddr(instance, name, &next);
                *h.stored = next;
            }
            *function = h.hook;
            return XR_SUCCESS;
        }
    }

    return m_nextGetInstanceProcAddr(instance, name, function);
}

XrResult HandOverlayLayer::xrCreateInstance(const XrInstanceCreateInfo*, XrInstance*) {
    return XR_SUCCESS;
}

// ── Session management ───────────────────────────────────────────────────────

XrResult HandOverlayLayer::xrCreateSession(XrInstance instance, const XrSessionCreateInfo* createInfo, XrSession* session) {
    XrResult result = m_nextCreateSession(instance, createInfo, session);
    if (XR_FAILED(result)) return result;

    if (!m_nextEnumerateSwapchainImages && m_nextGetInstanceProcAddr) {
        PFN_xrVoidFunction func = nullptr;
        m_nextGetInstanceProcAddr(instance, "xrEnumerateSwapchainImages", &func);
        m_nextEnumerateSwapchainImages = reinterpret_cast<PFN_xrEnumerateSwapchainImages>(func);
    }

    auto sd = std::make_unique<SessionData>();
    sd->session = *session;

    const void* next = createInfo->next;
    while (next) {
        auto* hdr = static_cast<const XrBaseInStructure*>(next);
        if (hdr->type == XR_TYPE_GRAPHICS_BINDING_D3D11_KHR) {
            auto* binding = reinterpret_cast<const XrGraphicsBindingD3D11KHR*>(next);
            sd->d3dDevice = binding->device;
            if (sd->d3dDevice) sd->d3dDevice->GetImmediateContext(&sd->d3dContext);
            break;
        }
        if (hdr->type == XR_TYPE_GRAPHICS_BINDING_D3D12_KHR) {
            auto* binding = reinterpret_cast<const XrGraphicsBindingD3D12KHR*>(next);
            sd->d3d12Device = binding->device;
            sd->d3d12Queue = binding->queue;
            sd->isD3D12 = true;

            // Create D3D11On12 device for rendering
            IUnknown* queues[] = { sd->d3d12Queue };
            HRESULT hr = D3D11On12CreateDevice(
                sd->d3d12Device,        // D3D12 device
                0,                       // flags
                nullptr, 0,              // feature levels
                queues, 1,               // command queues
                0,                       // node mask
                &sd->d3dDevice,          // out: D3D11 device
                &sd->d3dContext,          // out: D3D11 context
                nullptr                  // out: feature level
            );
            if (SUCCEEDED(hr) && sd->d3dDevice) {
                sd->d3dDevice->QueryInterface(__uuidof(ID3D11On12Device), (void**)&sd->d3d11on12);
            }
            logFmt("C:\\Temp\\MSFSHandOverlay_Session.log",
                "D3D11On12 created: hr=0x%08X dev=%p ctx=%p on12=%p\r\n",
                hr, (void*)sd->d3dDevice, (void*)sd->d3dContext, (void*)sd->d3d11on12);
            break;
        }
        next = hdr->next;
    }

    logFmt("C:\\Temp\\MSFSHandOverlay_Session.log",
        "xrCreateSession OK, d3d=%p d3d12=%d\r\n", (void*)sd->d3dDevice, sd->isD3D12 ? 1 : 0);

    m_sessions[*session] = std::move(sd);
    return result;
}

XrResult HandOverlayLayer::xrDestroySession(XrSession session) {
    auto it = m_sessions.find(session);
    if (it != m_sessions.end()) {
        it->second->pipeline.release();
        if (it->second->d3d11on12) it->second->d3d11on12->Release();
        if (it->second->d3dContext) it->second->d3dContext->Release();
        if (it->second->d3dDevice && it->second->isD3D12) it->second->d3dDevice->Release();
        m_sessions.erase(it);
    }
    // Release wrapped D3D12 resources for all remaining swapchains
    for (auto& [sc, resources] : m_wrappedD3D12Resources) {
        for (auto* r : resources) { if (r) r->Release(); }
    }
    m_wrappedD3D12Resources.clear();
    // Release QI'd textures from D3D12 swapchains
    for (auto& [sc, textures] : m_appSwapchainTextures) {
        for (auto* t : textures) { if (t) t->Release(); }
    }
    m_appSwapchainTextures.clear();
    m_appAcquiredIndex.clear();
    return m_nextDestroySession(session);
}

XrResult HandOverlayLayer::xrCreateReferenceSpace(XrSession session, const XrReferenceSpaceCreateInfo* ci, XrSpace* space) {
    return m_nextCreateReferenceSpace(session, ci, space);
}

// ── Swapchain tracking ───────────────────────────────────────────────────────

XrResult HandOverlayLayer::xrCreateSwapchain(XrSession session, const XrSwapchainCreateInfo* ci, XrSwapchain* swapchain) {
    XrResult result = m_nextCreateSwapchain(session, ci, swapchain);

    // Find session data to check if D3D12
    SessionData* sd = nullptr;
    for (auto& pair : m_sessions) {
        if (pair.first == session) { sd = pair.second.get(); break; }
    }
    bool useD3D12 = sd && sd->isD3D12 && sd->d3d11on12;

    if (XR_SUCCEEDED(result) && m_nextEnumerateSwapchainImages) {
        uint32_t count = 0;
        m_nextEnumerateSwapchainImages(*swapchain, 0, &count, nullptr);
        if (count > 0 && useD3D12) {
            // D3D12 path: enumerate as D3D12 textures, wrap via D3D11On12
            std::vector<XrSwapchainImageD3D12KHR> imgs(count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
            m_nextEnumerateSwapchainImages(*swapchain, count, &count,
                reinterpret_cast<XrSwapchainImageBaseHeader*>(imgs.data()));
            std::vector<ID3D11Texture2D*> texs;
            std::vector<ID3D11Resource*> wrapped;
            for (auto& i : imgs) {
                D3D11_RESOURCE_FLAGS d3d11Flags = {};
                d3d11Flags.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
                ID3D11Resource* wrappedRes = nullptr;
                HRESULT hr = sd->d3d11on12->CreateWrappedResource(
                    i.texture,
                    &d3d11Flags,
                    D3D12_RESOURCE_STATE_RENDER_TARGET,
                    D3D12_RESOURCE_STATE_RENDER_TARGET,
                    __uuidof(ID3D11Resource),
                    (void**)&wrappedRes);
                ID3D11Texture2D* tex = nullptr;
                if (SUCCEEDED(hr) && wrappedRes) {
                    wrappedRes->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex);
                }
                texs.push_back(tex);
                wrapped.push_back(wrappedRes);
            }
            m_appSwapchainTextures[*swapchain] = std::move(texs);
            m_wrappedD3D12Resources[*swapchain] = std::move(wrapped);
        } else if (count > 0) {
            // D3D11 path: enumerate as D3D11 textures directly
            std::vector<XrSwapchainImageD3D11KHR> imgs(count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
            m_nextEnumerateSwapchainImages(*swapchain, count, &count,
                reinterpret_cast<XrSwapchainImageBaseHeader*>(imgs.data()));
            std::vector<ID3D11Texture2D*> texs;
            for (auto& i : imgs) texs.push_back(i.texture);
            m_appSwapchainTextures[*swapchain] = std::move(texs);
        }
        logFmt("C:\\Temp\\MSFSHandOverlay_Swapchain.log",
            "App swapchain: handle=%p %ux%u fmt=%lld imgs=%u d3d12=%d\r\n",
            (void*)*swapchain, ci->width, ci->height, ci->format, count, useD3D12 ? 1 : 0);
    }
    return result;
}

XrResult HandOverlayLayer::xrDestroySwapchain(XrSwapchain swapchain) {
    // Release wrapped D3D12 resources
    auto wIt = m_wrappedD3D12Resources.find(swapchain);
    if (wIt != m_wrappedD3D12Resources.end()) {
        for (auto* r : wIt->second) { if (r) r->Release(); }
        m_wrappedD3D12Resources.erase(wIt);
    }
    // Release QI'd texture refs for D3D12 swapchains
    auto tIt = m_appSwapchainTextures.find(swapchain);
    if (tIt != m_appSwapchainTextures.end()) {
        // Only release if we created wrapped resources (D3D12 path)
        if (wIt != m_wrappedD3D12Resources.end()) {
            for (auto* t : tIt->second) { if (t) t->Release(); }
        }
        m_appSwapchainTextures.erase(tIt);
    }
    m_appAcquiredIndex.erase(swapchain);
    return m_nextDestroySwapchain(swapchain);
}

XrResult HandOverlayLayer::xrAcquireSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageAcquireInfo* ai, uint32_t* index) {
    XrResult result = m_nextAcquireSwapchainImage(swapchain, ai, index);
    if (XR_SUCCEEDED(result))
        m_appAcquiredIndex[swapchain] = *index;
    return result;
}

// ── MARK: capture the app's own render-pose query ─────────────────────────────

XrResult HandOverlayLayer::xrLocateViews(XrSession session, const XrViewLocateInfo* viewLocateInfo,
    XrViewState* viewState, uint32_t viewCapacityInput,
    uint32_t* viewCountOutput, XrView* views) {
    XrResult result = m_nextLocateViews(session, viewLocateInfo, viewState,
        viewCapacityInput, viewCountOutput, views);
    // Capture the poses the app itself just asked for, AND which space it queried them
    // against - only a pose queried against the same space the projection layer itself
    // uses (proj->space, checked at composite time) is safe to reuse; see header comment.
    if (XR_SUCCEEDED(result) && views && viewCountOutput && viewCapacityInput > 0) {
        uint32_t n = (*viewCountOutput < 2) ? *viewCountOutput : 2;
        for (uint32_t i = 0; i < n; i++) {
            m_locatedViewPose[i] = views[i].pose;
        }
        m_locatedViewSpace = viewLocateInfo->space;
        m_locatedViewCount = n;
        m_hasLocatedViews = (n > 0);
    }
    return result;
}

// ── Render pipeline initialisation ───────────────────────────────────────────

bool HandOverlayLayer::initRenderPipeline(SessionData* sd) {
    auto* dev = sd->d3dDevice;
    if (!dev) return false;

    // Compile vertex shader
    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* errBlob = nullptr;
    HRESULT hr = D3DCompile(OVERLAY_VS, strlen(OVERLAY_VS), "overlay_vs",
        nullptr, nullptr, "main", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vsBlob, &errBlob);
    if (FAILED(hr)) {
        if (errBlob) {
            logFmt("C:\\Temp\\MSFSHandOverlay_Shader.log",
                "VS compile error: %s\r\n", (char*)errBlob->GetBufferPointer());
            errBlob->Release();
        }
        return false;
    }

    dev->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &sd->pipeline.vs);

    // Input layout
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    dev->CreateInputLayout(layout, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &sd->pipeline.inputLayout);
    vsBlob->Release();

    // Compile pixel shader
    ID3DBlob* psBlob = nullptr;
    hr = D3DCompile(OVERLAY_PS, strlen(OVERLAY_PS), "overlay_ps",
        nullptr, nullptr, "main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &psBlob, &errBlob);
    if (FAILED(hr)) {
        if (errBlob) {
            logFmt("C:\\Temp\\MSFSHandOverlay_Shader.log",
                "PS compile error: %s\r\n", (char*)errBlob->GetBufferPointer());
            errBlob->Release();
        }
        return false;
    }
    dev->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &sd->pipeline.ps);
    psBlob->Release();

    // Blend state (alpha blending). The pixel shader (OVERLAY_PS) outputs PREMULTIPLIED
    // alpha (return float4(c.rgb * brightness * alpha, alpha)), so SrcBlend must be ONE, not
    // SRC_ALPHA - using SRC_ALPHA here double-multiplies alpha into the color (once in the
    // shader, again by the blend unit), producing alpha^2 instead of alpha. This was nearly
    // invisible with chroma-key's near-binary alpha (0^2=0, 1^2=1) but is very visible as
    // washed-out/ghostly transparency with AI Segmentation's smooth, continuous alpha values.
    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    dev->CreateBlendState(&bd, &sd->pipeline.blendState);

    // Sampler (linear, clamp)
    D3D11_SAMPLER_DESC sam = {};
    sam.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sam.AddressU = sam.AddressV = sam.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    dev->CreateSamplerState(&sam, &sd->pipeline.samplerState);

    // Rasterizer (no cull, no depth clip)
    D3D11_RASTERIZER_DESC rs = {};
    rs.FillMode = D3D11_FILL_SOLID;
    rs.CullMode = D3D11_CULL_NONE;
    rs.DepthClipEnable = FALSE;
    dev->CreateRasterizerState(&rs, &sd->pipeline.rasterizerState);

    // Depth stencil (disabled)
    D3D11_DEPTH_STENCIL_DESC ds = {};
    ds.DepthEnable = FALSE;
    ds.StencilEnable = FALSE;
    dev->CreateDepthStencilState(&ds, &sd->pipeline.depthStencilState);

    // Constant buffer (96 bytes: 64 matrix + 16 uvRect + 16 chromaKey)
    D3D11_BUFFER_DESC cb = {};
    cb.ByteWidth = 128;  // 64 viewProj + 16 uvRect + 16 chromaHSV + 16 chromaExtra + 16 extraParams
    cb.Usage = D3D11_USAGE_DYNAMIC;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    dev->CreateBuffer(&cb, nullptr, &sd->pipeline.constantBuffer);

    // Vertex buffer (4 verts, dynamic)
    D3D11_BUFFER_DESC vb = {};
    vb.ByteWidth = sizeof(OverlayVertex) * 4;
    vb.Usage = D3D11_USAGE_DYNAMIC;
    vb.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    dev->CreateBuffer(&vb, nullptr, &sd->pipeline.vertexBuffer);

    sd->pipeline.initialized = true;
    logMsg("C:\\Temp\\MSFSHandOverlay_Shader.log", "Render pipeline initialised OK\r\n");
    return true;
}

void HandOverlayLayer::ensureCameraTexture(SessionData* sd, uint32_t w, uint32_t h, uint32_t fmt) {
    if (sd->pipeline.cameraTexture && sd->pipeline.cameraTexWidth == w && sd->pipeline.cameraTexHeight == h)
        return;

    if (sd->pipeline.cameraSRV) { sd->pipeline.cameraSRV->Release(); sd->pipeline.cameraSRV = nullptr; }
    if (sd->pipeline.cameraTexture) { sd->pipeline.cameraTexture->Release(); sd->pipeline.cameraTexture = nullptr; }

    DXGI_FORMAT dxFmt = (fmt != 0) ? (DXGI_FORMAT)fmt : DXGI_FORMAT_B8G8R8A8_UNORM;

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = w;
    td.Height = h;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = dxFmt;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    sd->d3dDevice->CreateTexture2D(&td, nullptr, &sd->pipeline.cameraTexture);

    D3D11_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = dxFmt;
    srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = 1;
    sd->d3dDevice->CreateShaderResourceView(sd->pipeline.cameraTexture, &srv, &sd->pipeline.cameraSRV);

    sd->pipeline.cameraTexWidth = w;
    sd->pipeline.cameraTexHeight = h;

    logFmt("C:\\Temp\\MSFSHandOverlay_Shader.log",
        "Camera texture created: %ux%u fmt=%u\r\n", w, h, dxFmt);
}

DXGI_FORMAT HandOverlayLayer::getRTVFormat(DXGI_FORMAT fmt) {
    switch (fmt) {
        case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
        case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
        default: return fmt;
    }
}

// ── xrEndFrame ───────────────────────────────────────────────────────────────

XrResult HandOverlayLayer::xrEndFrame(XrSession session, const XrFrameEndInfo* frameEndInfo) {
    static int frameCount = 0, okCount = 0, failCount = 0;
    frameCount++;

    if (frameCount == 1 || frameCount % 300 == 0) {
        logFmt("C:\\Temp\\MSFSHandOverlay_EndFrame.log",
            "Frame %d: OK=%d FAIL=%d SHM=%s\r\n",
            frameCount, okCount, failCount,
            (m_sharedFrames && m_sharedFrames->isValid()) ? "YES" : "NO");
    }

    auto it = m_sessions.find(session);
    if (it != m_sessions.end() && frameEndInfo) {
        if (renderOntoProjection(it->second.get(), frameEndInfo))
            okCount++;
        else
            failCount++;
    }

    return m_nextEndFrame(session, frameEndInfo);
}

// ── Main rendering ───────────────────────────────────────────────────────────

bool HandOverlayLayer::renderOntoProjection(SessionData* sd, const XrFrameEndInfo* frameEndInfo) {
    static int dbg = 0;
    dbg++;
    bool shouldLog = (dbg <= 5) || (dbg % 300 == 0);

    // ── shared memory checks ────────────────────────────────────────
    if (!m_sharedFrames || !m_sharedFrames->isValid()) {
        if (!m_sharedFrames->openSharedMemory()) return false;
    }
    auto* hdr = m_sharedFrames->getHeader();
    if (!hdr || !hdr->frameReady) {
        if (shouldLog) logFmt("C:\\Temp\\MSFSHandOverlay_Composite.log",
            "[%d] BLOCKED: hdr=%p frameReady=%d\r\n", dbg, hdr, hdr ? (int)hdr->frameReady : -1);
        return false;
    }
    if (!sd->d3dContext) {
        if (shouldLog) logFmt("C:\\Temp\\MSFSHandOverlay_Composite.log",
            "[%d] BLOCKED: d3dContext is null\r\n", dbg);
        return false;
    }

    // ── stale frame detection (main app exited or crashed) ───────
    if (hdr->frameSequence == 0) {
        if (shouldLog) logFmt("C:\\Temp\\MSFSHandOverlay_Composite.log",
            "[%d] BLOCKED: frameSequence=0 (shutdown)\r\n", dbg);
        return false;  // Shutdown signal
    }
    if (hdr->frameSequence == sd->lastFrameSequence) {
        sd->staleFrameCount++;
        // After ~5 seconds of no new frames (300 VR frames at 90Hz),
        // assume the main app is gone and stop rendering
        if (sd->staleFrameCount > 300) {
            if (shouldLog) logFmt("C:\\Temp\\MSFSHandOverlay_Composite.log",
                "[%d] Stale frame detected (seq=%u unchanged for %u frames), hiding overlay\r\n",
                dbg, hdr->frameSequence, sd->staleFrameCount);
            return false;
        }
    } else {
        sd->staleFrameCount = 0;  // Reset on new frame
    }

    void* frameData = m_sharedFrames->getFrameData();
    if (!frameData) return false;

    // ── find projection layer ───────────────────────────────────────
    const XrCompositionLayerProjection* proj = nullptr;
    for (uint32_t i = 0; i < frameEndInfo->layerCount; i++) {
        if (frameEndInfo->layers[i]->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
            proj = reinterpret_cast<const XrCompositionLayerProjection*>(frameEndInfo->layers[i]);
            break;
        }
    }
    if (!proj || proj->viewCount < 2) {
        if (shouldLog) logFmt("C:\\Temp\\MSFSHandOverlay_Composite.log",
            "[%d] FAIL: no projection layer\r\n", dbg);
        return false;
    }

    // ── initialise pipeline ─────────────────────────────────────────
    if (!sd->pipeline.initialized) {
        if (!initRenderPipeline(sd)) {
            if (shouldLog) logFmt("C:\\Temp\\MSFSHandOverlay_Composite.log",
                "[%d] FAIL: pipeline init failed\r\n", dbg);
            return false;
        }
    }

    // ── camera texture ──────────────────────────────────────────────
    ensureCameraTexture(sd, hdr->width, hdr->height, hdr->format);
    if (!sd->pipeline.cameraTexture || !sd->pipeline.cameraSRV) return false;

    // Upload camera data every frame
    D3D11_BOX camBox = {0, 0, 0, hdr->width, hdr->height, 1};
    sd->d3dContext->UpdateSubresource(sd->pipeline.cameraTexture, 0, &camBox,
        frameData, hdr->stride, 0);

    // ── compute overlay world-space quad corners ────────────────────
    float hw = hdr->width_m / 2.0f;
    float hh = hdr->height_m / 2.0f;

    // Euler to quaternion (YXZ order – matches original code)
    float pr = hdr->pitch * 3.14159265f / 180.0f;
    float yr = hdr->yaw   * 3.14159265f / 180.0f;
    float rr = hdr->roll  * 3.14159265f / 180.0f;
    float cy = cosf(yr * 0.5f), sy = sinf(yr * 0.5f);
    float cp = cosf(pr * 0.5f), sp = sinf(pr * 0.5f);
    float cr = cosf(rr * 0.5f), sr = sinf(rr * 0.5f);
    XMVECTOR overlayQ = XMVectorSet(
        sr*cp*cy - cr*sp*sy,   // x
        cr*sp*cy + sr*cp*sy,   // y
        cr*cp*sy - sr*sp*cy,   // z
        cr*cp*cy + sr*sp*sy);  // w

    // Overlay position is fixed in world space (config posX/Y/Z), full stop - no per-frame
    // offset. Parallax comes entirely from rendering with the eye's real world-space pose
    // below (see xrLocateViews / header comment), which is what makes a world-fixed object
    // correctly slide relative to the view as the head moves, instead of following it.
    XMMATRIX overlayWorld = XMMatrixRotationQuaternion(overlayQ) *
        XMMatrixTranslation(hdr->posX, hdr->posY, hdr->posZ);

    // Local corners (triangle strip: BL, TL, BR, TR)
    XMVECTOR localC[4] = {
        XMVectorSet(-hw, -hh, 0, 1),  // BL  uv(0,1)
        XMVectorSet(-hw, +hh, 0, 1),  // TL  uv(0,0)
        XMVectorSet(+hw, -hh, 0, 1),  // BR  uv(1,1)
        XMVectorSet(+hw, +hh, 0, 1),  // TR  uv(1,0)
    };
    float uvs[4][2] = {{0,1},{0,0},{1,1},{1,0}};

    OverlayVertex verts[4];
    for (int i = 0; i < 4; i++) {
        XMFLOAT3 p;
        XMStoreFloat3(&p, XMVector3TransformCoord(localC[i], overlayWorld));
        verts[i] = {p.x, p.y, p.z, uvs[i][0], uvs[i][1]};
    }

    // Update vertex buffer
    D3D11_MAPPED_SUBRESOURCE mapped;
    sd->d3dContext->Map(sd->pipeline.vertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    memcpy(mapped.pData, verts, sizeof(verts));
    sd->d3dContext->Unmap(sd->pipeline.vertexBuffer, 0);

    // ── render for each eye ─────────────────────────────────────────
    for (uint32_t eye = 0; eye < 2 && eye < proj->viewCount; eye++) {
        const auto& view = proj->views[eye];
        XrSwapchain sc = view.subImage.swapchain;

        auto texIt = m_appSwapchainTextures.find(sc);
        auto idxIt = m_appAcquiredIndex.find(sc);
        if (texIt == m_appSwapchainTextures.end() || idxIt == m_appAcquiredIndex.end()) {
            if (shouldLog) logFmt("C:\\Temp\\MSFSHandOverlay_Composite.log",
                "[%d] SKIP eye %u: swapchain not tracked\r\n", dbg, eye);
            continue;
        }
        uint32_t imgIdx = idxIt->second;
        if (imgIdx >= texIt->second.size()) continue;

        ID3D11Texture2D* projTex = texIt->second[imgIdx];
        if (!projTex) continue;

        // D3D12: acquire wrapped resource before rendering
        ID3D11Resource* wrappedRes = nullptr;
        if (sd->d3d11on12) {
            auto wIt = m_wrappedD3D12Resources.find(sc);
            if (wIt != m_wrappedD3D12Resources.end() && imgIdx < wIt->second.size()) {
                wrappedRes = wIt->second[imgIdx];
                if (wrappedRes) sd->d3d11on12->AcquireWrappedResources(&wrappedRes, 1);
            }
        }
        D3D11_TEXTURE2D_DESC projDesc;
        projTex->GetDesc(&projDesc);

        // ── View matrix from eye pose ───────────────────────────────
        // Only trust the pose captured from xrLocateViews if it was queried against the
        // SAME XrSpace the app's projection layer is itself expressed in (proj->space) -
        // confirmed by logging that this app calls xrLocateViews against multiple
        // different XrSpace handles over a session, so blindly reusing the most recent
        // call silently mixed poses from unrelated coordinate origins. See header comment.
        bool locatedMatchesProjSpace = m_hasLocatedViews && eye < m_locatedViewCount &&
            m_locatedViewSpace == proj->space;
        const XrPosef& ep = locatedMatchesProjSpace ? m_locatedViewPose[eye] : view.pose;

        XMVECTOR eyeQ = XMVectorSet(ep.orientation.x, ep.orientation.y,
                                     ep.orientation.z, ep.orientation.w);

        // Light smoothing on orientation only (position stays raw/unsmoothed - it's
        // already carefully tuned for correct, responsive parallax, and smoothing
        // translation risks reintroducing the head-locked-feeling lag that was just
        // fixed). This just takes the edge off frame-to-frame tracking jitter/noise
        // during rotation without adding perceptible lag - 0.5 converges within ~2
        // frames at typical HMD refresh rates.
        if (!m_hasSmoothedOrientation[eye]) {
            m_smoothedOrientation[eye] = eyeQ;
            m_hasSmoothedOrientation[eye] = true;
        } else {
            m_smoothedOrientation[eye] = XMQuaternionSlerp(m_smoothedOrientation[eye], eyeQ, 0.5f);
        }

        XMMATRIX eyeWorld = XMMatrixRotationQuaternion(m_smoothedOrientation[eye]) *
            XMMatrixTranslation(ep.position.x, ep.position.y, ep.position.z);
        XMMATRIX viewMat = XMMatrixInverse(nullptr, eyeWorld);

        // ── Projection matrix from FOV (RH, depth [0,1]) ───────────
        const XrFovf& fov = view.fov;
        float nearZ = 0.01f, farZ = 100.0f;
        float l = nearZ * tanf(fov.angleLeft);
        float r = nearZ * tanf(fov.angleRight);
        float t = nearZ * tanf(fov.angleUp);
        float b = nearZ * tanf(fov.angleDown);
        XMMATRIX projMat = XMMatrixPerspectiveOffCenterRH(l, r, b, t, nearZ, farZ);

        // Combined VP
        XMFLOAT4X4 vp;
        XMStoreFloat4x4(&vp, viewMat * projMat);

        // ── UV rect (left or right half of SBS camera) ──────────────
        float uvRect[4];
        if (eye == 0) { uvRect[0]=0.0f; uvRect[1]=0.0f; uvRect[2]=0.5f; uvRect[3]=1.0f; }
        else           { uvRect[0]=0.5f; uvRect[1]=0.0f; uvRect[2]=1.0f; uvRect[3]=1.0f; }

        // ── Update constant buffer ──────────────────────────────────
        D3D11_MAPPED_SUBRESOURCE cbMap;
        sd->d3dContext->Map(sd->pipeline.constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &cbMap);
        {
            float* d = (float*)cbMap.pData;
            memcpy(d, &vp, 64);                          // viewProj (16 floats)
            d[16] = uvRect[0]; d[17] = uvRect[1];        // uvRect
            d[18] = uvRect[2]; d[19] = uvRect[3];
            // Per-eye chroma key HSV (hue in degrees, matching archive ChromaKey.hlsl)
            if (eye == 0) {
                d[20] = hdr->chromaLeftHueCenter;
                d[21] = hdr->chromaLeftHueRange;
                d[22] = hdr->chromaLeftSatMin;
                d[23] = hdr->chromaLeftValMin;
                d[24] = hdr->chromaLeftEdgeSoftness;
            } else {
                d[20] = hdr->chromaRightHueCenter;
                d[21] = hdr->chromaRightHueRange;
                d[22] = hdr->chromaRightSatMin;
                d[23] = hdr->chromaRightValMin;
                d[24] = hdr->chromaRightEdgeSoftness;
            }
            d[25] = hdr->borderMaskSize;
            d[26] = hdr->borderMaskSoftness;
            d[27] = (float)hdr->inverseMode;
            // extraParams
            d[28] = hdr->handBrightness;
            d[29] = (float)hdr->alphaMode;  // 0 = legacy chroma-key, 1 = ML alpha already in texture
            d[30] = 0.0f;
            d[31] = 0.0f;
        }
        sd->d3dContext->Unmap(sd->pipeline.constantBuffer, 0);

        // ── Create RTV ──────────────────────────────────────────────
        D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format = getRTVFormat(projDesc.Format);
        if (projDesc.ArraySize > 1) {
            rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
            rtvDesc.Texture2DArray.MipSlice = 0;
            rtvDesc.Texture2DArray.FirstArraySlice = view.subImage.imageArrayIndex;
            rtvDesc.Texture2DArray.ArraySize = 1;
        } else {
            rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            rtvDesc.Texture2D.MipSlice = 0;
        }

        ID3D11RenderTargetView* rtv = nullptr;
        HRESULT hr = sd->d3dDevice->CreateRenderTargetView(projTex, &rtvDesc, &rtv);
        if (FAILED(hr)) {
            if (shouldLog) logFmt("C:\\Temp\\MSFSHandOverlay_Composite.log",
                "[%d] FAIL eye %u: CreateRTV hr=0x%08X fmt=%u\r\n", dbg, eye, hr, projDesc.Format);
            continue;
        }

        // ── Set viewport ────────────────────────────────────────────
        D3D11_VIEWPORT vp_d3d;
        vp_d3d.TopLeftX = (float)view.subImage.imageRect.offset.x;
        vp_d3d.TopLeftY = (float)view.subImage.imageRect.offset.y;
        vp_d3d.Width    = (float)view.subImage.imageRect.extent.width;
        vp_d3d.Height   = (float)view.subImage.imageRect.extent.height;
        vp_d3d.MinDepth = 0.0f;
        vp_d3d.MaxDepth = 1.0f;

        // ── Bind pipeline and draw ──────────────────────────────────
        auto* ctx = sd->d3dContext;
        ctx->IASetInputLayout(sd->pipeline.inputLayout);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        UINT stride = sizeof(OverlayVertex), offset = 0;
        ctx->IASetVertexBuffers(0, 1, &sd->pipeline.vertexBuffer, &stride, &offset);
        ctx->VSSetShader(sd->pipeline.vs, nullptr, 0);
        ctx->VSSetConstantBuffers(0, 1, &sd->pipeline.constantBuffer);
        ctx->RSSetViewports(1, &vp_d3d);
        ctx->RSSetState(sd->pipeline.rasterizerState);
        ctx->PSSetShader(sd->pipeline.ps, nullptr, 0);
        ctx->PSSetConstantBuffers(0, 1, &sd->pipeline.constantBuffer);
        ctx->PSSetShaderResources(0, 1, &sd->pipeline.cameraSRV);
        ctx->PSSetSamplers(0, 1, &sd->pipeline.samplerState);
        ctx->OMSetRenderTargets(1, &rtv, nullptr);
        ctx->OMSetBlendState(sd->pipeline.blendState, nullptr, 0xFFFFFFFF);
        ctx->OMSetDepthStencilState(sd->pipeline.depthStencilState, 0);

        ctx->Draw(4, 0);

        // Unbind
        ID3D11ShaderResourceView* nullSRV = nullptr;
        ID3D11RenderTargetView* nullRTV = nullptr;
        ctx->PSSetShaderResources(0, 1, &nullSRV);
        ctx->OMSetRenderTargets(1, &nullRTV, nullptr);
        rtv->Release();

        // D3D12: release wrapped resource after rendering
        if (wrappedRes && sd->d3d11on12) {
            sd->d3d11on12->ReleaseWrappedResources(&wrappedRes, 1);
        }
    }

    sd->d3dContext->Flush();
    sd->lastFrameSequence = hdr->frameSequence;

    if (shouldLog) {
        logFmt("C:\\Temp\\MSFSHandOverlay_Composite.log",
            "[%d] OK: cam %ux%u, overlay pos(%.2f,%.2f,%.2f) locatedViews=%d size(%.2f,%.2f)\r\n",
            dbg, hdr->width, hdr->height,
            hdr->posX, hdr->posY, hdr->posZ,
            m_hasLocatedViews ? 1 : 0,
            hdr->width_m, hdr->height_m);
    }

    return true;
}

// ── Static hook wrappers ─────────────────────────────────────────────────────

static XrResult XRAPI_CALL Hook_xrCreateSession(XrInstance i, const XrSessionCreateInfo* ci, XrSession* s)  { return g_layer ? g_layer->xrCreateSession(i,ci,s) : XR_ERROR_HANDLE_INVALID; }
static XrResult XRAPI_CALL Hook_xrDestroySession(XrSession s)                                              { return g_layer ? g_layer->xrDestroySession(s) : XR_ERROR_HANDLE_INVALID; }
static XrResult XRAPI_CALL Hook_xrEndFrame(XrSession s, const XrFrameEndInfo* fi)                          { return g_layer ? g_layer->xrEndFrame(s,fi) : XR_ERROR_HANDLE_INVALID; }
static XrResult XRAPI_CALL Hook_xrCreateSwapchain(XrSession s, const XrSwapchainCreateInfo* ci, XrSwapchain* sc) { return g_layer ? g_layer->xrCreateSwapchain(s,ci,sc) : XR_ERROR_HANDLE_INVALID; }
static XrResult XRAPI_CALL Hook_xrDestroySwapchain(XrSwapchain sc)                                         { return g_layer ? g_layer->xrDestroySwapchain(sc) : XR_ERROR_HANDLE_INVALID; }
static XrResult XRAPI_CALL Hook_xrCreateReferenceSpace(XrSession s, const XrReferenceSpaceCreateInfo* ci, XrSpace* sp) { return g_layer ? g_layer->xrCreateReferenceSpace(s,ci,sp) : XR_ERROR_HANDLE_INVALID; }
static XrResult XRAPI_CALL Hook_xrAcquireSwapchainImage(XrSwapchain sc, const XrSwapchainImageAcquireInfo* ai, uint32_t* idx) { return g_layer ? g_layer->xrAcquireSwapchainImage(sc,ai,idx) : XR_ERROR_HANDLE_INVALID; }
static XrResult XRAPI_CALL Hook_xrLocateViews(XrSession s, const XrViewLocateInfo* vi, XrViewState* vs, uint32_t cap, uint32_t* count, XrView* views) { return g_layer ? g_layer->xrLocateViews(s,vi,vs,cap,count,views) : XR_ERROR_HANDLE_INVALID; }

} // namespace APILayer
} // namespace MSFSHandOverlay

// ── OpenXR Layer negotiation ─────────────────────────────────────────────────

extern "C" {

static XRAPI_ATTR XrResult XRAPI_CALL LayerXrGetInstanceProcAddr(
    XrInstance instance, const char* name, PFN_xrVoidFunction* function) {
    if (!MSFSHandOverlay::APILayer::g_layer) return XR_ERROR_HANDLE_INVALID;
    return MSFSHandOverlay::APILayer::g_layer->xrGetInstanceProcAddr(instance, name, function);
}

static XRAPI_ATTR XrResult XRAPI_CALL LayerXrCreateApiLayerInstance(
    const XrInstanceCreateInfo* info, const XrApiLayerCreateInfo* layerInfo, XrInstance* instance) {

    logMsg("C:\\Temp\\MSFSHandOverlay_CreateInstance.log", ">>> xrCreateApiLayerInstance\r\n");

    XrApiLayerCreateInfo modInfo = *layerInfo;
    modInfo.nextInfo = layerInfo->nextInfo->next;

    auto nextCreate = (PFN_xrCreateApiLayerInstance)layerInfo->nextInfo->nextCreateApiLayerInstance;
    if (!nextCreate) return XR_ERROR_INITIALIZATION_FAILED;

    XrResult result = nextCreate(info, &modInfo, instance);

    if (result == XR_SUCCESS && MSFSHandOverlay::APILayer::g_layer && layerInfo->nextInfo) {
        // Reset all cached function pointers — they are instance-specific and become
        // stale when another layer (e.g. motion compensation) creates a dummy instance
        // during its setup, or when VR is re-entered with a new XrInstance.
        MSFSHandOverlay::APILayer::g_layer = std::make_unique<MSFSHandOverlay::APILayer::HandOverlayLayer>();
        MSFSHandOverlay::APILayer::g_layer->setNextGetInstanceProcAddr(layerInfo->nextInfo->nextGetInstanceProcAddr);
    }

    logFmt("C:\\Temp\\MSFSHandOverlay_CreateInstance.log",
        "<<< result=%d\r\n", (int)result);
    return result;
}

extern "C" XRAPI_ATTR XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(
    const XrNegotiateLoaderInfo* loaderInfo, const char* layerName,
    XrNegotiateApiLayerRequest* apiLayerRequest) {

    logMsg("C:\\Temp\\MSFSHandOverlay_Negotiate.log", ">>> NEGOTIATE\r\n");

    if (loaderInfo->structType != XR_LOADER_INTERFACE_STRUCT_LOADER_INFO ||
        loaderInfo->structVersion != XR_LOADER_INFO_STRUCT_VERSION ||
        loaderInfo->structSize != sizeof(XrNegotiateLoaderInfo))
        return XR_ERROR_INITIALIZATION_FAILED;

    if (apiLayerRequest->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST ||
        apiLayerRequest->structVersion != XR_API_LAYER_INFO_STRUCT_VERSION ||
        apiLayerRequest->structSize != sizeof(XrNegotiateApiLayerRequest))
        return XR_ERROR_INITIALIZATION_FAILED;

    apiLayerRequest->layerInterfaceVersion = XR_CURRENT_LOADER_API_LAYER_VERSION;
    apiLayerRequest->layerApiVersion = XR_CURRENT_API_VERSION;

    if (!MSFSHandOverlay::APILayer::g_layer)
        MSFSHandOverlay::APILayer::g_layer = std::make_unique<MSFSHandOverlay::APILayer::HandOverlayLayer>();

    apiLayerRequest->getInstanceProcAddr = LayerXrGetInstanceProcAddr;
    apiLayerRequest->createApiLayerInstance = LayerXrCreateApiLayerInstance;

    logMsg("C:\\Temp\\MSFSHandOverlay_Negotiate.log", "<<< SUCCESS\r\n");
    return XR_SUCCESS;
}

} // extern "C"
