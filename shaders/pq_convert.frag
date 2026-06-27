// HDR PQ (ST.2084) encoding fragment shader
// Reads linear intermediate (BT.709 primaries, normalized, 1.0 = 350 nit paper white),
// converts primaries BT.709 -> BT.2020 (HDR10), scales to absolute nit, applies
// ST.2084 PQ OETF, outputs to swapchain.

#version 450

layout(binding = 0) uniform sampler2D texLinear;  // linear intermediate (R16G16B16A16_SFLOAT)

layout(push_constant) uniform PQPush {
    float uMaxNit;        // clamp ceiling
} pc;

// Must match common.h PAPER_WHITE_NIT. Linear 1.0 == this many nit.
const float PAPER_WHITE_NIT = 350.0;

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

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

// BT.709 -> BT.2020 primaries (linear). Rows sum to 1.0, all entries >= 0:
// GLSL mat3 is column-major: columns below are input R, G, B -> output R, G, B.
const mat3 BT709_TO_BT2020 = mat3(
    0.627404, 0.069097, 0.016391,
    0.329283, 0.919540, 0.088013,
    0.043313, 0.011362, 0.895595);

void main() {
    vec3 linear709  = texture(texLinear, fragUV).rgb;     // [0,1] BT.709-linear
    vec3 linear2020 = BT709_TO_BT2020 * linear709;        // [0,1] BT.2020-linear
    vec3 nitVal     = linear2020 * PAPER_WHITE_NIT;       // convert to nit
    vec3 clamped    = clamp(nitVal, 0.0, pc.uMaxNit);
    outColor = vec4(linearToPQ(clamped), 1.0);
}
