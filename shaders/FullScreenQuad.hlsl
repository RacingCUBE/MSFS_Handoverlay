// FullScreenQuad.hlsl - Simple vertex shader for full-screen quad rendering

struct VSInput {
    float3 position : POSITION;
    float2 texCoord : TEXCOORD0;
};

struct PSInput {
    float4 position : SV_POSITION;
    float2 tex : TEXCOORD0;
};

// Vertex Shader Main
PSInput VSMain(VSInput input) {
    PSInput output;
    output.position = float4(input.position, 1.0f);
    output.tex = input.texCoord;
    return output;
}
