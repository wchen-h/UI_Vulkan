// SDR merged shader: UI mixing (BT.709 linear or sRGB domain) + sRGB encode
// Only draws background within local region (2× UI quad linear = 4× area).
// Outside local region: black.
// Foreground UI (texAlpha > 0.5) and Background UI (texAlpha <= 0.5) use separate
// alpha controls.
// blendingMode (offset 52, repurposed from bgYScale): 0=linear blend, 1=sRGB blend

#version 450

layout(binding = 0) uniform sampler2D texRGB;    // sRGB texture -> hardware decodes to linear
layout(binding = 1) uniform sampler2D texAlpha;  // linear alpha texture
layout(binding = 2) uniform sampler2D texBG;     // background image (sRGB -> linear BT.709)

layout(push_constant) uniform FragPush {
    // bytes 0-15: vertex (offset + scale) — full-screen: offset=(0,0), scale=(2,2)
    layout(offset = 16) float fgAlpha;        // foreground alpha
    layout(offset = 20) float bgMultiplier;    // background brightness multiplier
    layout(offset = 24) float fgYScale;       // unused in SDR (layout compat with HDR)
    layout(offset = 28) float cbcrScale;      // unused in SDR (layout compat with HDR)
    layout(offset = 32) vec2  uiOffset;        // UI area bottom-left in screen UV [0,1]
    layout(offset = 40) vec2  uiScale;         // UI area size in screen UV [0,1]
    layout(offset = 48) float bgAlpha;        // background alpha
    layout(offset = 52) float blendingMode;   // 0=linear blend, 1=sRGB blend (SDR only)
} fpc;

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

// sRGB transfer functions (IEC 61966-2-1)
vec3 linearToSRGB(vec3 c) {
    bvec3 mask = lessThanEqual(c, vec3(0.0031308));
    vec3 lo = c * 12.92;
    vec3 hi = pow(c, vec3(1.0 / 2.4)) * 1.055 - 0.055;
    return mix(hi, lo, mask);
}

vec3 sRGBToLinear(vec3 c) {
    bvec3 mask = lessThanEqual(c, vec3(0.04045));
    vec3 lo = c / 12.92;
    vec3 hi = pow((c + 0.055) / 1.055, vec3(2.4));
    return mix(hi, lo, mask);
}

void main() {
    // Local region = 2× UI quad linear (4× area), centered, clamped to [0,1]
    vec2 localMin = max(vec2(0.5) - fpc.uiScale, vec2(0.0));
    vec2 localMax = min(vec2(0.5) + fpc.uiScale, vec2(1.0));
    bool insideLocal = (fragUV.x >= localMin.x && fragUV.x <= localMax.x &&
                        fragUV.y >= localMin.y && fragUV.y <= localMax.y);

    if (!insideLocal) {
        // Outside local region: black
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // 1. Sample background (sRGB -> linear, only in local region)
    vec3 bgRGB = texture(texBG, fragUV).rgb;
    vec3 bg    = bgRGB * fpc.bgMultiplier;

    // 2. Compute UI UV from full-screen UV
    vec2 uiUV = (fragUV - fpc.uiOffset) / fpc.uiScale;
    bool insideUI = (uiUV.x >= 0.0 && uiUV.x <= 1.0 &&
                     uiUV.y >= 0.0 && uiUV.y <= 1.0);

    if (!insideUI) {
        // Inside local but outside UI: just show scaled background
        outColor = vec4(bg, 1.0);
        return;
    }

    // 3. Sample UI (sRGB -> linear, [0,1])
    vec3  uiRGB    = texture(texRGB, uiUV).rgb;
    float texAlpha = texture(texAlpha, uiUV).r;

    // Foreground (texAlpha > 0.5) and Background (texAlpha <= 0.5)
    // use separate alpha sliders.
    float sliderAlpha = (texAlpha > 0.5) ? fpc.fgAlpha : fpc.bgAlpha;
    float effAlpha = texAlpha * sliderAlpha;

    vec3 blended;
    if (fpc.blendingMode > 0.5) {
        // sRGB domain blending: convert to sRGB, blend, convert back to linear
        vec3 uiSRGB  = linearToSRGB(uiRGB);
        vec3 bgSRGB  = linearToSRGB(bg);
        vec3 blendedSRGB = uiSRGB * effAlpha + bgSRGB * (1.0 - effAlpha);
        blended = sRGBToLinear(blendedSRGB);
    } else {
        // Linear domain blending (default)
        blended = uiRGB * effAlpha + bg * (1.0 - effAlpha);
    }

    outColor = vec4(blended, 1.0);
}
