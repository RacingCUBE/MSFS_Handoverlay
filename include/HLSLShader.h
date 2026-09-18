#pragma once

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <string>

using Microsoft::WRL::ComPtr;

/**
 * @brief HLSL Shader wrapper for DirectX 11
 *
 * Compiles and manages vertex and pixel shaders, constant buffers, and input layouts.
 */
class HLSLShader {
public:
    HLSLShader();
    ~HLSLShader();

    // Load and compile shaders from file
    bool loadFromFiles(ID3D11Device* device, const std::wstring& vsPath, const std::wstring& psPath);

    // Load and compile shaders from source code
    bool loadFromSource(ID3D11Device* device, const std::string& vsSource, const std::string& psSource);

    // Bind shader to pipeline
    void bind(ID3D11DeviceContext* context);
    void unbind(ID3D11DeviceContext* context);

    // Constant buffer management
    bool createConstantBuffer(ID3D11Device* device, UINT size);
    void updateConstantBuffer(ID3D11DeviceContext* context, const void* data, UINT size);

    // Accessors
    ID3D11VertexShader* getVertexShader() const { return m_vertexShader.Get(); }
    ID3D11PixelShader* getPixelShader() const { return m_pixelShader.Get(); }
    ID3D11InputLayout* getInputLayout() const { return m_inputLayout.Get(); }

private:
    ComPtr<ID3D11VertexShader> m_vertexShader;
    ComPtr<ID3D11PixelShader> m_pixelShader;
    ComPtr<ID3D11InputLayout> m_inputLayout;
    ComPtr<ID3D11Buffer> m_constantBuffer;
    ComPtr<ID3DBlob> m_vertexShaderBlob;  // Keep for input layout creation

    bool compileShader(const std::wstring& filename, const char* entryPoint, const char* profile, ID3DBlob** outBlob);
    bool compileShaderFromSource(const std::string& source, const char* entryPoint, const char* profile, ID3DBlob** outBlob);
    bool createInputLayout(ID3D11Device* device);
};

/**
 * @brief Constant buffer structure for ChromaKey shader
 */
struct ChromaKeyConstants {
    // RGB-based keying
    float targetColor[3];   // Target chroma key color (RGB 0-1)
    float similarity;       // Color distance threshold (0.0-1.0)

    float smoothness;       // Edge blending (0.0-0.2)
    float spill;            // Spill suppression (0.0-1.0)
    float padding1[2];      // Alignment to 16 bytes

    // HSV-based keying
    float hueCenter;        // Target hue (0-360 degrees)
    float hueRange;         // Hue tolerance
    float saturationMin;    // Min saturation to key
    float valueMin;         // Min brightness to key

    float edgeSoftness;     // Edge smoothing
    float borderMaskSize;   // Border gradient size (0.0-0.5)
    float borderSoftness;   // Border gradient softness
    int useHSVMode;         // 0 = RGB mode, 1 = HSV mode

    int inverseMode;        // 0 = remove background, 1 = keep target
    float padding2[3];      // Alignment to 16 bytes
};
