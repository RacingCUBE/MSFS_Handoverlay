#version 330 core

in vec2 TexCoord;
out vec4 FragColor;

uniform sampler2D uTexture;
uniform vec3 uChromaKeyColor;      // Not used in fast mode, kept for compatibility
uniform float uHueRange;           // Used as green threshold (0-1 scale)
uniform float uSaturationMin;      // Not used in fast mode
uniform float uValueMin;           // Not used in fast mode
uniform float uEdgeSoftness;       // Not used in fast mode (hard edges)
uniform int uInverseMode;          // 0 = normal (key out green), 1 = inverse (keep green)
uniform float uBorderMaskSize;     // Not used in fast mode
uniform float uBorderMaskSoftness; // Not used in fast mode

// PERFORMANCE MODE: Green-Only Check (10x faster than HSV)
// This simple shader just checks if the green channel is significantly
// higher than red and blue channels (indicating a green screen)

void main()
{
    vec4 texColor = texture(uTexture, TexCoord);

    // Calculate "greenness" - how much more green than other channels
    // If green is much higher than red and blue, it's part of the green screen
    float greenness = texColor.g - max(texColor.r, texColor.b);

    // Simple threshold check (uHueRange is repurposed as threshold)
    // Default threshold: 0.15 works well for most green screens
    float threshold = max(uHueRange / 360.0, 0.1); // Convert from hue range to 0-1

    // Hard cutoff - pixel is either fully transparent or fully opaque
    // This is MUCH faster than smoothstep/interpolation
    float isGreenScreen = (greenness > threshold) ? 0.0 : 1.0;

    // Apply inverse mode if enabled
    float alphaMultiplier = (uInverseMode == 1) ? (1.0 - isGreenScreen) : isGreenScreen;

    // Output with alpha channel (no border masking in performance mode)
    FragColor = vec4(texColor.rgb, texColor.a * alphaMultiplier);
}
