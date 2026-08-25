// HDR merged shader (mixed_ui_test): mix UI+BG FIRST, then Y-Scale on mixed result.
// Non-separated approach: Y-Scale applied after mixing, not before.
// Only draws background within local region (2x UI quad linear = 4x area).
// Outside local region: black.
// Foreground UI (texAlpha > 0.5) and Background UI (texAlpha <= 0.5) use separate
// Alpha and Y-Scale controls. Y-Scale is applied to the MIXED result (not UI alone).
// CbCr-Scale is kept for future use (currently applied to mixed Cb/Cr).

#version 450

layout(binding = 0) uniform sampler2D texRGB;    // sRGB texture -> hardware decodes to linear
layout(binding = 1) uniform sampler2D texAlpha;  // linear alpha texture
layout(binding = 2) uniform sampler2D texBG;     // background image (sRGB -> linear BT.709)

layout(push_constant) uniform FragPush {
    // bytes 0-15: vertex (offset + scale) — full-screen: offset=(0,0), scale=(2,2)
    layout(offset = 16) float fgAlpha;        // foreground alpha (mix alpha, not eff)
    layout(offset = 20) float bgMultiplier;    // background brightness multiplier
    layout(offset = 24) float fgYScale;       // foreground Y-Scale (applied to MIXED result)
    layout(offset = 28) float cbcrScale;      // CbCr scale factor (shared, on mixed result)
    layout(offset = 32) vec2  uiOffset;        // UI area bottom-left in screen UV [0,1]
    layout(offset = 40) vec2  uiScale;         // UI area size in screen UV [0,1]
    layout(offset = 48) float bgAlpha;        // background alpha (mix alpha, not eff)
    layout(offset = 52) float bgYScale;       // background Y-Scale (applied to MIXED result)
} fpc;

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

const float PAPER_WHITE_NIT = 350.0;

// BT.709 -> BT.2020 primaries (linear). Column-major (GLSL mat3).
const mat3 BT709_TO_BT2020 = mat3(
    0.627404, 0.069097, 0.016391,
    0.329283, 0.919540, 0.088013,
    0.043313, 0.011362, 0.895595);

// ST.2084 PQ OETF: linear nit -> PQ code [0,1]
vec3 linearToPQ(vec3 linearNits) {
    const float m1 = 2610.0 / 16384.0;
    const float m2 = 2523.0 / 32.0;
    const float c1 = 3424.0 / 4096.0;
    const float c2 = 2413.0 / 128.0;
    const float c3 = 2392.0 / 128.0;

    vec3 y    = linearNits / 10000.0;
    vec3 yPow = pow(max(y, vec3(0.0)), vec3(m1));
    vec3 num  = c1 + c2 * yPow;
    vec3 den  = 1.0 + c3 * yPow;
    return pow(num / den, vec3(m2));
}

// ST.2084 PQ EOTF: PQ code [0,1] -> linear nit
vec3 pqDecode(vec3 pq) {
    const float m1 = 2610.0 / 16384.0;
    const float m2 = 2523.0 / 32.0;
    const float c1 = 3424.0 / 4096.0;
    const float c2 = 2413.0 / 128.0;
    const float c3 = 2392.0 / 128.0;

    vec3 vp  = pow(max(pq, vec3(0.0)), vec3(1.0 / m2));
    vec3 num = max(vp - vec3(c1), vec3(0.0));
    vec3 den = max(vec3(c2) - vec3(c3) * vp, vec3(0.0001));
    return 10000.0 * pow(num / den, vec3(1.0 / m1));
}

// BT.2020 YCbCr conversion (10-bit full range, on PQ non-linear values)
vec3 rgb2ycbcr(vec3 rgb) {
    float Y  = 0.2627 * rgb.r + 0.6780 * rgb.g + 0.0593 * rgb.b;
    float Cb = (-0.1396 * rgb.r - 0.3604 * rgb.g + 0.5000 * rgb.b) + 512.0;
    float Cr = (0.5000 * rgb.r - 0.4598 * rgb.g - 0.0402 * rgb.b) + 512.0;
    return vec3(Y, Cb, Cr);
}

// Inverse BT.2020 YCbCr -> RGB (10-bit full range)
vec3 ycbcr2rgb(vec3 ycbcr) {
    float Y  = ycbcr.x;
    float Cb = ycbcr.y - 512.0;
    float Cr = ycbcr.z - 512.0;
    float R  = Y + 1.4746 * Cr;
    float B  = Y + 1.8814 * Cb;
    float G  = (Y - 0.2627 * R - 0.0593 * B) / 0.6780;
    return vec3(R, G, B);
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

    // 1. Sample background (sRGB -> linear BT.709, only in local region)
    vec3 bgRGB   = texture(texBG, fragUV).rgb;
    vec3 bgNit_raw    = BT709_TO_BT2020 * (bgRGB * PAPER_WHITE_NIT);  // raw, no multiplier
    vec3 bgNit_scaled = bgNit_raw * fpc.bgMultiplier;                  // adjusted by BG Nit slider

    // 2. Compute UI UV from full-screen UV
    vec2 uiUV = (fragUV - fpc.uiOffset) / fpc.uiScale;
    bool insideUI = (uiUV.x >= 0.0 && uiUV.x <= 1.0 &&
                     uiUV.y >= 0.0 && uiUV.y <= 1.0);

    if (!insideUI) {
        // Inside local but outside UI: show scaled background
        outColor = vec4(linearToPQ(bgNit_scaled), 1.0);
        return;
    }

    // 3. Sample UI (sRGB -> linear BT.709)
    vec3  uiRGB    = texture(texRGB, uiUV).rgb;
    float texAlpha = texture(texAlpha, uiUV).r;

    // 4. UI -> BT.2020 nit
    vec3 uiNit709  = uiRGB * PAPER_WHITE_NIT;
    vec3 uiNit2020 = BT709_TO_BT2020 * uiNit709;

    // 5. Non-separated: mix UI with bg FIRST (linear nits domain)
    //    Then apply Y-Scale on the MIXED result (not UI alone).
    //
    //    IMPORTANT: UI-covered pixels (alpha != 0) use RAW bg (no bgMultiplier).
    //    BG Nit slider only affects non-UI background. UI area brightness
    //    is controlled solely by Y-Scale. This lets the operator independently
    //    set B (via BG Nit) and adjust Y-Scale without interference.
    if (texAlpha > 0.0) {
        // Compute alpha for mixing (fg/bg distinction by texAlpha threshold)
        float sliderAlpha = (texAlpha > 0.5) ? fpc.fgAlpha : fpc.bgAlpha;
        float alpha = clamp(texAlpha * min(sliderAlpha, 1.0)
                          + max(0.0, sliderAlpha - 1.0), 0.0, 1.0);

        // Mix in linear nits domain (raw bg, NOT bgMultiplier-adjusted)
        vec3 mixedNit = uiNit2020 * alpha + bgNit_raw * (1.0 - alpha);

        // 6. PQ encode mixed -> 10-bit -> YCbCr
        vec3 pq_mixed = linearToPQ(mixedNit);
        vec3 rgb10    = pq_mixed * 1023.0;
        vec3 ycbcr    = rgb2ycbcr(rgb10);

        // 7. Y x Y-Scale (on MIXED result, not UI alone)
        float yScale = (texAlpha > 0.5) ? fpc.fgYScale : fpc.bgYScale;
        ycbcr.x = ycbcr.x * yScale;
        // CbCr-Scale (kept for future use, currently on mixed Cb/Cr)
        ycbcr.y = 512.0 + (ycbcr.y - 512.0) * fpc.cbcrScale;
        ycbcr.z = 512.0 + (ycbcr.z - 512.0) * fpc.cbcrScale;

        // 8. Clamp + YCbCr -> RGB + /1023 -> PQ output
        ycbcr = clamp(ycbcr, vec3(0.0), vec3(1023.0));
        vec3 rgb10_adj = ycbcr2rgb(ycbcr);
        outColor = vec4(rgb10_adj / 1023.0, 1.0);
    } else {
        // texAlpha == 0: no UI, show scaled background (BG Nit controlled)
        outColor = vec4(linearToPQ(bgNit_scaled), 1.0);
    }
}
