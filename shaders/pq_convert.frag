// HDR PQ (ST.2084) encoding fragment shader
// Reads linear intermediate (normalized, 1.0 = 500 nit white point),
// converts to absolute nit, applies ST.2084 PQ OETF, outputs to swapchain.
//
// Used by the right-half HDR path of the conversion pass.

#version 450

layout(binding = 0) uniform sampler2D texLinear;  // linear intermediate (R16G16B16A16_SFLOAT)

layout(push_constant) uniform PQPush {
    float uWhitePoint;    // nit value of 1.0 in intermediate (default 500)
    float uBG_nit;        // HDR background brightness in nit
    float uMaxNit;        // clamp ceiling
    float uHDRSupported;  // 1.0 = HDR active, 0.0 = fallback (gray)
    // UI quad bounds to detect non-UI pixels for bg replacement
    float uUI_left;       // NDC x for UI left edge
    float uUI_right;
    float uUI_bottom;
    float uUI_top;
} pc;

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

// ST.2084 Perceptual Quantizer OETF
vec3 linearToPQ(vec3 linearNits) {
    const float m1 = 2610.0 / 16384.0;   // 0.1593017578125
    const float m2 = 2523.0 / 32.0;      // 78.84375
    const float c1 = 3424.0 / 4096.0;    // 0.8359375
    const float c2 = 2413.0 / 128.0;     // 18.8515625
    const float c3 = 2392.0 / 128.0;     // 18.6875

    vec3 y    = linearNits / 10000.0;    // normalize to [0,1]
    vec3 yPow = pow(max(y, vec3(0.0)), vec3(m1));
    vec3 num  = c1 + c2 * yPow;
    vec3 den  = 1.0 + c3 * yPow;
    return pow(num / den, vec3(m2));
}

void main() {
    vec3 linear = texture(texLinear, fragUV).rgb;   // [0,1] normalized
    vec3 nitVal = linear * pc.uWhitePoint;           // convert to nit

    // Determine if pixel is inside UI area (in NDC space)
    // fragUV is in [0,1] → convert to NDC [-1,1]
    float ndcX = fragUV.x * 2.0 - 1.0;
    float ndcY = fragUV.y * 2.0 - 1.0;
    bool insideUI = (ndcX >= pc.uUI_left && ndcX <= pc.uUI_right
                  && ndcY >= pc.uUI_bottom && ndcY <= pc.uUI_top);

    // Outside UI area: use pure HDR background
    if (!insideUI) {
        nitVal = vec3(pc.uBG_nit);
    }

    vec3 clamped = clamp(nitVal, 0.0, pc.uMaxNit);

    if (pc.uHDRSupported < 0.5) {
        // HDR not supported: show dark gray right half
        outColor = vec4(0.08, 0.08, 0.08, 1.0);
    } else {
        // [DIAG] 红=UI区域内 绿=UI区域外
        outColor = insideUI ? vec4(1.0, 0.0, 0.0, 1.0) : vec4(0.0, 1.0, 0.0, 1.0);
    }
}
