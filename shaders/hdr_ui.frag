// HDR merged shader (mixed_ui_test): mix UI+BG FIRST, then Y-Scale on mixed result.
// Non-separated approach: Y-Scale applied after mixing, not before.
// Only draws background within local region (2x UI quad linear = 4x area).
// Outside local region: black.
// Foreground UI (texAlpha > 0.5) and Background UI (texAlpha <= 0.5) use separate
// Alpha controls. Y-Scale is unified (single fgYScale for all UI pixels).
// CbCr-Scale is kept for future use (currently applied to mixed Cb/Cr).
// blendingMode: 0=linear alpha blend, 1=sRGB alpha blend (in BT.709 domain).

#version 450

layout(binding = 0) uniform sampler2D texRGB;    // sRGB texture -> hardware decodes to linear BT.709
layout(binding = 1) uniform sampler2D texAlpha;  // linear alpha texture
layout(binding = 2) uniform sampler2D texBG;     // background image (sRGB -> linear BT.709)

layout(push_constant) uniform FragPush {
    // bytes 0-15: vertex (offset + scale) — full-screen: offset=(0,0), scale=(2,2)
    layout(offset = 16) float fgAlpha;        // foreground alpha (plain mix alpha)
    layout(offset = 20) float bgMultiplier;    // background brightness multiplier (non-UI bg only)
    layout(offset = 24) float fgYScale;       // Y-Scale (applied to MIXED result, unified)
    layout(offset = 28) float cbcrScale;      // CbCr scale factor (shared, on mixed result)
    layout(offset = 32) vec2  uiOffset;        // UI area bottom-left in screen UV [0,1]
    layout(offset = 40) vec2  uiScale;         // UI area size in screen UV [0,1]
    layout(offset = 48) float bgAlpha;        // background alpha (plain mix alpha)
    layout(offset = 52) float blendingMode;   // 0=linear blend, 1=sRGB blend
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

// sRGB transfer functions (IEC 61966-2-1) — for sRGB-domain alpha blending in BT.709
vec3 linearToSRGB(vec3 c) {
    bvec3 mask = lessThanEqual(c, vec3(0.0031308));
    vec3 lo = c * 12.92;
    vec3 hi = pow(max(c, vec3(0.0)), vec3(1.0 / 2.4)) * 1.055 - 0.055;
    return mix(hi, lo, mask);
}

vec3 sRGBToLinear(vec3 c) {
    bvec3 mask = lessThanEqual(c, vec3(0.04045));
    vec3 lo = c / 12.92;
    vec3 hi = pow((c + 0.055) / 1.055, vec3(2.4));
    return mix(hi, lo, mask);
}

void main() {
    // Local region = 2x UI quad linear (4x area), centered, clamped to [0,1]
    vec2 localMin = max(vec2(0.5) - fpc.uiScale, vec2(0.0));
    vec2 localMax = min(vec2(0.5) + fpc.uiScale, vec2(1.0));
    bool insideLocal = (fragUV.x >= localMin.x && fragUV.x <= localMax.x &&
                        fragUV.y >= localMin.y && fragUV.y <= localMax.y);

    if (!insideLocal) {
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // 1. Sample background (sRGB -> hardware decodes to linear BT.709 [0,1])
    vec3 bgRGB        = texture(texBG, fragUV).rgb;
    vec3 bgNit_raw    = BT709_TO_BT2020 * (bgRGB * PAPER_WHITE_NIT);  // raw, no multiplier
    vec3 bgNit_scaled = bgNit_raw * fpc.bgMultiplier;                 // adjusted by BG Nit slider

    // 2. Compute UI UV from full-screen UV
    vec2 uiUV = (fragUV - fpc.uiOffset) / fpc.uiScale;
    bool insideUI = (uiUV.x >= 0.0 && uiUV.x <= 1.0 &&
                     uiUV.y >= 0.0 && uiUV.y <= 1.0);

    if (!insideUI) {
        // Inside local but outside UI: show scaled background
        outColor = vec4(linearToPQ(bgNit_scaled), 1.0);
        return;
    }

    // 3. Sample UI (sRGB -> hardware decodes to linear BT.709 [0,1])
    vec3  uiRGB    = texture(texRGB, uiUV).rgb;
    float texAlpha = texture(texAlpha, uiUV).r;

    // 4. UI -> BT.2020 nit (for linear blend path)
    vec3 uiNit2020 = BT709_TO_BT2020 * (uiRGB * PAPER_WHITE_NIT);

    // 5. Non-separated: mix UI with bg FIRST, then apply Y-Scale on the MIXED result.
    //
    //    UI-covered pixels (alpha != 0) use RAW bg (no bgMultiplier).
    //    Non-UI pixels (alpha == 0) use scaled bg (bgMultiplier applied).
    //    NOTE: This creates a brightness seam at the UI edge when bgMultiplier != 1.
    //    This is intentional — BG Nit controls non-UI bg only, Y-Scale controls UI area.
    if (texAlpha > 0.0) {
        // Plain mix alpha (not eff-alpha): texAlpha * sliderAlpha, clamped
        float sliderAlpha = (texAlpha > 0.5) ? fpc.fgAlpha : fpc.bgAlpha;
        float alpha = clamp(texAlpha * sliderAlpha, 0.0, 1.0);

        vec3 mixedNit;
        if (fpc.blendingMode > 0.5) {
            // sRGB domain blending in BT.709 (texture's native domain).
            // uiRGB and bgRGB are linear BT.709 [0,1] (hardware sRGB decode).
            // Encode to sRGB, blend in sRGB domain, decode back to linear BT.709.
            vec3 ui_srgb    = linearToSRGB(uiRGB);
            vec3 bg_srgb    = linearToSRGB(bgRGB);
            vec3 mixed_srgb = ui_srgb * alpha + bg_srgb * (1.0 - alpha);
            vec3 mixed_lin  = sRGBToLinear(mixed_srgb);
            mixedNit = BT709_TO_BT2020 * (mixed_lin * PAPER_WHITE_NIT);
        } else {
            // Linear domain blending (in BT.2020 nits — linear blend commutes with matrix)
            mixedNit = uiNit2020 * alpha + bgNit_raw * (1.0 - alpha);
        }

        // 6. PQ encode mixed -> 10-bit -> YCbCr
        vec3 pq_mixed = linearToPQ(mixedNit);
        vec3 rgb10    = pq_mixed * 1023.0;
        vec3 ycbcr    = rgb2ycbcr(rgb10);

        // 7. Y x Y-Scale (on MIXED result, unified for all UI pixels)
        ycbcr.x = ycbcr.x * fpc.fgYScale;
        // CbCr-Scale (kept for future use)
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
