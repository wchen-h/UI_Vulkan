// sRGB encoding fragment shader (post-process pass)
// Reads linear intermediate, optionally encodes to sRGB for swapchain
//
// Push constant: uSRGBEncode (1.0 = encode, 0.0 = passthrough)
//   - swapchain UNORM → uSRGBEncode=1.0, shader does sRGB encoding
//   - swapchain SRGB  → uSRGBEncode=0.0, hardware handles it, shader passes through

#version 450

layout(binding = 0) uniform sampler2D texLinear;  // linear intermediate (R16G16B16A16_SFLOAT)

layout(push_constant) uniform SRGBPush {
    float uSRGBEncode;
} pc;

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

// sRGB transfer function (IEC 61966-2-1)
vec3 linearToSRGB(vec3 c) {
    bvec3 mask = lessThanEqual(c, vec3(0.0031308));
    vec3 lo = c * 12.92;
    vec3 hi = pow(c, vec3(1.0 / 2.4)) * 1.055 - 0.055;
    return mix(hi, lo, mask);
}

void main() {
    vec3 linear = texture(texLinear, fragUV).rgb;
    vec3 encoded = mix(linear, linearToSRGB(linear), pc.uSRGBEncode);
    outColor = vec4(encoded, 1.0);
}
