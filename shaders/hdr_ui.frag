// HDR merged shader: UI mixing (BT.2020 linear) + PQ encode + YCbCr adjustment
// Merges old ui.frag + pq_convert.frag into one pass

#version 450

layout(binding = 0) uniform sampler2D texRGB;    // sRGB texture -> hardware decodes to linear
layout(binding = 1) uniform sampler2D texAlpha;  // linear alpha texture

layout(push_constant) uniform FragPush {
    // bytes 0-15: vertex (offset + scale) — shared range
    layout(offset = 16) float alpha;       // effAlpha (UI opacity multiplier)
    layout(offset = 20) float bgNit;       // background brightness in nit
    layout(offset = 24) float yScale;      // Y scale factor (1.0 = no change)
    layout(offset = 28) float cbcrScale;   // CbCr scale factor (1.0 = no change)
} fpc;

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

const float PAPER_WHITE_NIT = 350.0;

// BT.709 -> BT.2020 primaries (linear). Column-major (GLSL mat3).
// Rows sum to 1.0: white maps to white.
const mat3 BT709_TO_BT2020 = mat3(
    0.627404, 0.069097, 0.016391,
    0.329283, 0.919540, 0.088013,
    0.043313, 0.011362, 0.895595);

// ST.2084 Perceptual Quantizer OETF
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
// RGB in [0,1023] -> Y in [0,1023], Cb/Cr in [0,1023] (512 = zero chroma)
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
    // 1. Sample UI (sRGB -> linear BT.709)
    vec3  uiRGB    = texture(texRGB, fragUV).rgb;     // [0,1] BT.709-linear
    float texAlpha = texture(texAlpha, fragUV).r;     // original per-pixel alpha

    // 2. x PAPER_WHITE -> nit domain (BT.709)
    vec3 uiNit709 = uiRGB * PAPER_WHITE_NIT;

    // 3. BT.709 -> BT.2020 (in nit domain)
    vec3 uiNit2020 = BT709_TO_BT2020 * uiNit709;

    // 4. Mix with BG (BT.2020 linear, nit domain)
    //    BG is gray (R=G=B=bgNit), identical in BT.709 and BT.2020
    float effAlpha = texAlpha * fpc.alpha;
    vec3  bgNit2020 = vec3(fpc.bgNit);
    vec3  blended  = uiNit2020 * effAlpha + bgNit2020 * (1.0 - effAlpha);

    // 5. PQ encode (nit -> PQ code value [0,1])
    vec3 pq = linearToPQ(blended);

    // 6. x 1023 -> 10-bit full range
    vec3 rgb10 = pq * 1023.0;

    // 7. RGB -> YCbCr (BT.2020, on PQ non-linear values)
    vec3 ycbcr = rgb2ycbcr(rgb10);

    // 8. Adjust Y and CbCr (only for pixels with original alpha != 0)
    if (texAlpha > 0.0) {
        ycbcr.x = ycbcr.x * fpc.yScale;
        ycbcr.y = 512.0 + (ycbcr.y - 512.0) * fpc.cbcrScale;
        ycbcr.z = 512.0 + (ycbcr.z - 512.0) * fpc.cbcrScale;
    }

    // 9. Clamp to 10-bit range
    ycbcr = clamp(ycbcr, vec3(0.0), vec3(1023.0));

    // 10. YCbCr -> RGB
    vec3 rgb10_adj = ycbcr2rgb(ycbcr);

    // 11. / 1023 -> [0,1] PQ
    vec3 pq_adj = rgb10_adj / 1023.0;

    // 12. Output to swapchain (HDR10 PQ)
    outColor = vec4(pq_adj, 1.0);
}
