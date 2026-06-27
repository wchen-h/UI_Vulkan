// SDR merged shader: UI mixing (BT.709 linear) + sRGB encode
// Merges old ui.frag + srgb_convert.frag into one pass

#version 450

layout(binding = 0) uniform sampler2D texRGB;    // sRGB texture -> hardware decodes to linear
layout(binding = 1) uniform sampler2D texAlpha;  // linear alpha texture

layout(push_constant) uniform FragPush {
    // bytes 0-15: vertex (offset + scale) — shared range
    layout(offset = 16) float alpha;       // sdrAlpha (UI opacity multiplier)
    layout(offset = 20) float bgLinear;    // background gray in linear [0,1] (e.g. 0.18)
} fpc;

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
    vec3  uiRGB   = texture(texRGB, fragUV).rgb;      // hardware sRGB->linear, [0,1]
    float texAlpha = texture(texAlpha, fragUV).r;     // original per-pixel alpha

    float effAlpha = texAlpha * fpc.alpha;
    vec3  bg = vec3(fpc.bgLinear);
    vec3  blended = uiRGB * effAlpha + bg * (1.0 - effAlpha);

    outColor = vec4(blended, 1.0);  // linear output, srgb_convert handles encoding
}
