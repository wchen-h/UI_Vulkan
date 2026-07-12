// HDR merged shader: UI adjustment (BT.2020 PQ ICtCp) + then mix with BG texture
// Only draws background within local region (2× UI quad linear = 4× area).
// Outside local region: black.
// Foreground UI (texAlpha > 0.5) and Background UI (texAlpha <= 0.5) use separate
// I-Scale and Eff.Alpha controls. CtCp-Scale is shared.
//
// ICtCp pipeline (replaces YCbCr):
//   BT.2020 linear nit → LMS(normalized) → ×10000 → PQ → ICtCp
//   → I×iScale, Ct×ctScale, Cp×ctScale
//   → inverse ICtCp → LMS → PQ decode → BT.2020 linear nit
//   → mix with BG in linear domain → PQ encode → output

#version 450

layout(binding = 0) uniform sampler2D texRGB;    // sRGB texture -> hardware decodes to linear
layout(binding = 1) uniform sampler2D texAlpha;  // linear alpha texture
layout(binding = 2) uniform sampler2D texBG;     // background image (sRGB -> linear BT.709)

layout(push_constant) uniform FragPush {
    layout(offset = 16) float fgAlpha;         // foreground Eff.Alpha
    layout(offset = 20) float bgMultiplierI;    // background I multiplier (PQ domain)
    layout(offset = 24) float fgIScale;        // foreground I-Scale
    layout(offset = 28) float ctCpScale;       // CtCp scale factor (shared)
    layout(offset = 32) vec2  uiOffset;         // UI area bottom-left in screen UV [0,1]
    layout(offset = 40) vec2  uiScale;          // UI area size in screen UV [0,1]
    layout(offset = 48) float bgAlpha;         // background Eff.Alpha
    layout(offset = 52) float bgIScale;        // background I-Scale
} fpc;

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

const float PAPER_WHITE_NIT = 350.0;

// BT.709 -> BT.2020 primaries (linear). Column-major (GLSL mat3).
const mat3 BT709_TO_BT2020 = mat3(
    0.627404, 0.069097, 0.016391,
    0.329283, 0.919540, 0.088013,
    0.043313, 0.011362, 0.895595);

// RGB(BT.2020 linear, [0,1]) -> LMS (per BT.2100-2, no normalization)
const mat3 RGB2LMS = mat3(
     0.35913200, -0.19218800,  0.00704100,
     0.69760300,  1.10047100,  0.07440800,
    -0.03583400,  0.07543100,  0.84348700);

// LMS(PQ) -> ICtCp
const mat3 LMS2ICTCP = mat3(
     0.5,  1.613, -0.5,
     0.5, -1.613, -0.5,
     0.0,  0.0,    1.0);

// ICtCp -> LMS(PQ)
const mat3 ICTCP2LMS = mat3(
     1.0,         1.0,          1.0,
     0.3099814,  -0.3099814,    0.0,
     0.0,         0.0,          1.0);

// LMS(normalized) -> RGB(BT.2020 linear, [0,1])
const mat3 LMS2RGB = mat3(
     2.07055810,  0.36499726, -0.04948211,
    -1.32652256,  0.68039088, -0.04894738,
     0.20659157, -0.04533947,  1.18782982);

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

// BT.2020 linear RGB (nit) -> ICtCp
vec3 rgbToICtCp(vec3 rgbNit) {
    vec3 lms = RGB2LMS * (rgbNit / 10000.0);   // normalized LMS [0,1]
    vec3 lmsPQ = linearToPQ(lms * 10000.0);          // PQ encode
    return LMS2ICTCP * lmsPQ;                         // ICtCp
}

// ICtCp -> BT.2020 linear RGB (nit)
vec3 ictcpToRGB(vec3 ic) {
    vec3 lmsPQ = clamp(ICTCP2LMS * ic, 0.0, 1.0);   // LMS PQ, clamped
    vec3 lms = pqDecode(lmsPQ) / 10000.0;             // normalized LMS [0,1]
    vec3 rgbNorm = LMS2RGB * lms;                // RGB [0,1]
    return max(rgbNorm * 10000.0, vec3(0.0));          // RGB nit
}

void main() {
    // Local region = 2× UI quad linear (4× area), centered, clamped to [0,1]
    vec2 localMin = max(vec2(0.5) - fpc.uiScale, vec2(0.0));
    vec2 localMax = min(vec2(0.5) + fpc.uiScale, vec2(1.0));
    bool insideLocal = (fragUV.x >= localMin.x && fragUV.x <= localMax.x &&
                        fragUV.y >= localMin.y && fragUV.y <= localMax.y);

    if (!insideLocal) {
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // 1. Sample background (sRGB -> linear BT.709 -> BT.2020 nit)
    vec3 bgRGB   = texture(texBG, fragUV).rgb;
    vec3 bgNit   = BT709_TO_BT2020 * (bgRGB * PAPER_WHITE_NIT);

    // 2. BG: convert to ICtCp, scale I, convert back to linear
    vec3 bgICtCp = rgbToICtCp(bgNit);
    bgICtCp.x = clamp(bgICtCp.x * fpc.bgMultiplierI, 0.0, 1.0);
    vec3 bgNitAdj = ictcpToRGB(bgICtCp);

    // 3. Compute UI UV from full-screen UV
    vec2 uiUV = (fragUV - fpc.uiOffset) / fpc.uiScale;
    bool insideUI = (uiUV.x >= 0.0 && uiUV.x <= 1.0 &&
                     uiUV.y >= 0.0 && uiUV.y <= 1.0);

    if (!insideUI) {
        outColor = vec4(linearToPQ(bgNitAdj), 1.0);
        return;
    }

    // 4. Sample UI (sRGB -> linear BT.709 -> BT.2020 nit)
    vec3  uiRGB    = texture(texRGB, uiUV).rgb;
    float texAlpha = texture(texAlpha, uiUV).r;

    // 5. UI: convert to ICtCp, scale I/Ct/Cp, convert back to linear
    vec3 uiNit2020 = BT709_TO_BT2020 * (uiRGB * PAPER_WHITE_NIT);
    vec3 uiICtCp = rgbToICtCp(uiNit2020);

    float iScale = 1.0;
    float effAlpha = 0.0;
    if (texAlpha > 0.0) {
        float sliderAlpha;
        if (texAlpha > 0.5) {
            sliderAlpha = fpc.fgAlpha;
            iScale = fpc.fgIScale;
        } else {
            sliderAlpha = fpc.bgAlpha;
            iScale = fpc.bgIScale;
        }
        effAlpha = clamp(texAlpha * min(sliderAlpha, 1.0) + max(0.0, sliderAlpha - 1.0), 0.0, 1.0);
        uiICtCp.x = clamp(uiICtCp.x * iScale, 0.0, 1.0);
        uiICtCp.y = uiICtCp.y * fpc.ctCpScale;
        uiICtCp.z = uiICtCp.z * fpc.ctCpScale;
    }

    vec3 uiAdjNit = ictcpToRGB(uiICtCp);

    // 6. Mix with BG in LINEAR domain
    vec3 mixed = uiAdjNit * effAlpha + bgNitAdj * (1.0 - effAlpha);

    // 7. PQ encode -> output
    outColor = vec4(linearToPQ(mixed), 1.0);
}
