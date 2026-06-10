// HDR PQ (ST.2084) encoding fragment shader
// Reads linear intermediate (normalized, 1.0 = 500 nit white point),
// converts to absolute nit, applies ST.2084 PQ OETF, outputs to swapchain.

#version 450

layout(binding = 0) uniform sampler2D texLinear;  // linear intermediate (R16G16B16A16_SFLOAT)

layout(push_constant) uniform PQPush {
    float uMaxNit;        // clamp ceiling
    float uHDRSupported;  // 1.0 = HDR active, 0.0 = fallback (gray)
} pc;

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

void main() {
    vec3 linear = texture(texLinear, fragUV).rgb;   // [0,1] normalized
    vec3 nitVal = linear * 350.0;                    // convert to nit

    vec3 clamped = clamp(nitVal, 0.0, pc.uMaxNit);

    if (pc.uHDRSupported < 0.5) {
        outColor = vec4(0.08, 0.08, 0.08, 1.0);
    } else {
        outColor = vec4(linearToPQ(clamped), 1.0);
    }
}
