// ChromaKey.hlsl - High-performance chroma key shader for DirectX 11
// Optimized for hand tracking with smooth edge blending

// Input texture from webcam
Texture2D shaderTexture : register(t0);
SamplerState SampleType : register(s0);

// Parameters sent from C++
cbuffer ChromaBuffer : register(b0) {
    float3 targetColor;     // The chroma key color (e.g., Green in RGB 0-1)
    float similarity;       // How close the color must be to key out (0.0-1.0, e.g., 0.4)
    float smoothness;       // Edge blending/softness (0.0-0.2, e.g., 0.08)
    float spill;            // Spill suppression (0.0-1.0, reduces color bleed)

    // HSV-based keying parameters (more robust than RGB)
    float hueCenter;        // Target hue in degrees (0-360)
    float hueRange;         // Hue tolerance in degrees
    float saturationMin;    // Minimum saturation to key out (0-1)
    float valueMin;         // Minimum brightness to key out (0-1)
    float edgeSoftness;     // Edge smoothing factor

    // Border mask (to hide webcam black borders)
    float borderMaskSize;   // Size of border gradient (0.0-0.5)
    float borderSoftness;   // Softness of border transition

    int useHSVMode;         // 0 = RGB distance mode, 1 = HSV mode
    int inverseMode;        // 0 = remove background, 1 = remove everything except target
};

struct PixelInputType {
    float4 position : SV_POSITION;
    float2 tex : TEXCOORD0;
};

// Convert RGB to HSV
float3 rgb2hsv(float3 rgb) {
    float maxVal = max(max(rgb.r, rgb.g), rgb.b);
    float minVal = min(min(rgb.r, rgb.g), rgb.b);
    float delta = maxVal - minVal;

    float3 hsv;

    // Hue calculation
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

    // Saturation
    hsv.y = (maxVal < 0.00001) ? 0.0 : (delta / maxVal);

    // Value
    hsv.z = maxVal;

    return hsv;
}

// Calculate hue distance (wraps around 360 degrees)
float hueDistance(float h1, float h2) {
    float diff = abs(h1 - h2);
    if (diff > 180.0) {
        diff = 360.0 - diff;
    }
    return diff;
}

// Border gradient mask (fade edges to transparent)
float borderMask(float2 uv, float maskSize, float softness) {
    if (maskSize < 0.001) return 1.0;

    float2 dist = min(uv, 1.0 - uv);
    float minDist = min(dist.x, dist.y);

    float fadeStart = maskSize - softness;
    float fadeEnd = maskSize;

    return smoothstep(fadeStart, fadeEnd, minDist);
}

// Main pixel shader
float4 main(PixelInputType input) : SV_TARGET {
    float4 color = shaderTexture.Sample(SampleType, input.tex);
    float alpha = 1.0;

    if (useHSVMode == 1) {
        // === HSV-based chroma keying (more robust) ===
        float3 hsv = rgb2hsv(color.rgb);

        // Calculate hue distance from target
        float hueDiff = hueDistance(hsv.x, hueCenter);

        // Check if pixel is within hue range, saturation, and value thresholds
        float hueMatch = smoothstep(hueRange + edgeSoftness, hueRange - edgeSoftness, hueDiff);
        float satMatch = smoothstep(saturationMin - edgeSoftness, saturationMin + edgeSoftness, hsv.y);
        float valMatch = smoothstep(valueMin - edgeSoftness, valueMin + edgeSoftness, hsv.z);

        // Combine matches
        float keyAmount = hueMatch * satMatch * valMatch;

        if (inverseMode == 1) {
            // Inverse mode: keep target color, remove everything else
            alpha = keyAmount;
        } else {
            // Normal mode: remove target color, keep everything else
            alpha = 1.0 - keyAmount;
        }

    } else {
        // === RGB distance-based keying (simpler, faster) ===
        float d = distance(color.rgb, targetColor);

        // Calculate alpha using smoothstep for clean edges
        alpha = smoothstep(similarity, similarity + smoothness, d);

        // Spill suppression (reduce green/blue color bleed on hands)
        if (spill > 0.0 && alpha > 0.0) {
            float spillAmount = 1.0 - smoothstep(similarity, similarity + smoothness * 2.0, d);
            color.rgb = lerp(color.rgb, color.rgb * float3(1.0, 1.0 - spill * spillAmount, 1.0 - spill * spillAmount), spillAmount);
        }
    }

    // Apply border mask to fade edges
    float border = borderMask(input.tex, borderMaskSize, borderSoftness);
    alpha *= border;

    // Clamp alpha to [0, 1]
    alpha = saturate(alpha);

    // Return color with computed alpha (premultiplied alpha for better blending)
    return float4(color.rgb * alpha, alpha);
}
