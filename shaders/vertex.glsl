#version 330 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aTexCoord;

out vec2 TexCoord;

uniform bool uFlipVertical = true;   // Flip Y for DirectX (OpenGL is bottom-up, DirectX is top-down)
uniform bool uFlipHorizontal = true; // Flip X to fix camera mirroring

void main()
{
    gl_Position = vec4(aPosition, 1.0);

    // Apply horizontal flip if needed (fixes camera mirroring)
    float x = uFlipHorizontal ? (1.0 - aTexCoord.x) : aTexCoord.x;

    // Apply vertical flip if needed (DirectX compatibility)
    float y = uFlipVertical ? (1.0 - aTexCoord.y) : aTexCoord.y;

    TexCoord = vec2(x, y);
}
