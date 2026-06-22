// UI fragment shader (render to linear intermediate)
// Blends UI RGB + alpha over background in LINEAR domain
// Used by both SDR (bg=0.18) and HDR (bg=BG_nit/350.0) paths.

#version 450

layout(binding = 0) uniform sampler2D texRGB;    // sRGB texture → hardware decodes to linear
layout(binding = 1) uniform sampler2D texAlpha;  // linear alpha texture

layout(push_constant) uniform FragPush {
    // bytes 0-15: vertex (offset + scale) — shared range
    layout(offset = 16) float uiAlphaMultiplier; // bytes 16-19
    layout(offset = 20) float bgLinear;           // bytes 20-23
    layout(offset = 24) float uiLumMult;          // bytes 24-27 (SDR=1.0, HDR=uiLumRatio)
    layout(offset = 28) float chromaScale;        // bytes 28-31 (SDR=1.0, HDR=adjustable)
} fpc;

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

void main() {
    vec3  uiRGB   = texture(texRGB, fragUV).rgb;       // hardware sRGB→linear
    float uiAlpha = texture(texAlpha, fragUV).r;
    uiAlpha      *= fpc.uiAlphaMultiplier;

    // Apply brightness multiplier to UI
    vec3 uiAdj = uiRGB * fpc.uiLumMult;

    vec3 blended = uiAdj * uiAlpha + fpc.bgLinear * (1.0 - uiAlpha);

    float lum = 0.2126 * blended.r + 0.7152 * blended.g + 0.0722 * blended.b;
    vec3 gray = vec3(lum);
    vec3 result = gray + fpc.chromaScale * (blended - gray);

    outColor = vec4(result, 1.0);
}
