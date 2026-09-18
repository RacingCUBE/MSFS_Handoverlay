#include "HLSLShader.h"
#include <iostream>
#include <fstream>
#include <sstream>

HLSLShader::HLSLShader() {
}

HLSLShader::~HLSLShader() {
}

bool HLSLShader::loadFromFiles(ID3D11Device* device, const std::wstring& vsPath, const std::wstring& psPath) {
    // Compile vertex shader
    ComPtr<ID3DBlob> vsBlob;
    if (!compileShader(vsPath, "VSMain", "vs_5_0", vsBlob.GetAddressOf())) {
        std::cerr << "Failed to compile vertex shader: " << std::endl;
        return false;
    }

    // Create vertex shader
    HRESULT hr = device->CreateVertexShader(
        vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize(),
        nullptr,
        m_vertexShader.GetAddressOf()
    );

    if (FAILED(hr)) {
        std::cerr << "Failed to create vertex shader" << std::endl;
        return false;
    }

    // Store vertex shader blob for input layout creation
    m_vertexShaderBlob = vsBlob;

    // Compile pixel shader
    ComPtr<ID3DBlob> psBlob;
    if (!compileShader(psPath, "main", "ps_5_0", psBlob.GetAddressOf())) {
        std::cerr << "Failed to compile pixel shader" << std::endl;
        return false;
    }

    // Create pixel shader
    hr = device->CreatePixelShader(
        psBlob->GetBufferPointer(),
        psBlob->GetBufferSize(),
        nullptr,
        m_pixelShader.GetAddressOf()
    );

    if (FAILED(hr)) {
        std::cerr << "Failed to create pixel shader" << std::endl;
        return false;
    }

    // Create input layout
    if (!createInputLayout(device)) {
        std::cerr << "Failed to create input layout" << std::endl;
        return false;
    }

    std::cout << "HLSL shaders loaded successfully" << std::endl;
    return true;
}

bool HLSLShader::compileShader(const std::wstring& filename, const char* entryPoint, const char* profile, ID3DBlob** outBlob) {
    UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;

#ifdef _DEBUG
    compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3DCompileFromFile(
        filename.c_str(),
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entryPoint,
        profile,
        compileFlags,
        0,
        outBlob,
        errorBlob.GetAddressOf()
    );

    if (FAILED(hr)) {
        if (errorBlob) {
            std::cerr << "Shader compilation error:\n"
                      << static_cast<char*>(errorBlob->GetBufferPointer()) << std::endl;
        }
        return false;
    }

    return true;
}

bool HLSLShader::createInputLayout(ID3D11Device* device) {
    // Define input layout for full-screen quad
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };

    HRESULT hr = device->CreateInputLayout(
        layout,
        ARRAYSIZE(layout),
        m_vertexShaderBlob->GetBufferPointer(),
        m_vertexShaderBlob->GetBufferSize(),
        m_inputLayout.GetAddressOf()
    );

    if (FAILED(hr)) {
        std::cerr << "Failed to create input layout" << std::endl;
        return false;
    }

    return true;
}

void HLSLShader::bind(ID3D11DeviceContext* context) {
    context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    context->PSSetShader(m_pixelShader.Get(), nullptr, 0);
    context->IASetInputLayout(m_inputLayout.Get());

    if (m_constantBuffer) {
        context->PSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
    }
}

void HLSLShader::unbind(ID3D11DeviceContext* context) {
    context->VSSetShader(nullptr, nullptr, 0);
    context->PSSetShader(nullptr, nullptr, 0);
}

bool HLSLShader::createConstantBuffer(ID3D11Device* device, UINT size) {
    // Round up to 16-byte alignment (D3D11 requirement)
    size = (size + 15) & ~15;

    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.ByteWidth = size;
    bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = device->CreateBuffer(&bufferDesc, nullptr, m_constantBuffer.GetAddressOf());
    if (FAILED(hr)) {
        std::cerr << "Failed to create constant buffer" << std::endl;
        return false;
    }

    return true;
}

void HLSLShader::updateConstantBuffer(ID3D11DeviceContext* context, const void* data, UINT size) {
    if (!m_constantBuffer) return;

    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = context->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (SUCCEEDED(hr)) {
        memcpy(mapped.pData, data, size);
        context->Unmap(m_constantBuffer.Get(), 0);
    }
}
