// UI fragment shader (render to linear intermediate)
// Blends UI RGB + alpha over background in LINEAR domain
// Used by both SDR (bg=0.18) and HDR (bg=BG_nit/500.0) paths.

#version 450

layout(binding = 0) uniform sampler2D texRGB;    // sRGB texture → hardware decodes to linear
layout(binding = 1) uniform sampler2D texAlpha;  // linear alpha texture

layout(push_constant) uniform FragPush {
    // bytes 0-15: vertex (offset + scale) — shared range
    layout(offset = 16) float uiAlphaMultiplier; // bytes 16-19
    layout(offset = 20) float bgLinear;           // bytes 20-23 (SDR=0.18, HDR=BG_nit/500.0)
} fpc;

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

void main() {
    vec3  uiRGB   = texture(texRGB, fragUV).rgb;       // hardware sRGB→linear
    float uiAlpha = texture(texAlpha, fragUV).r;
    uiAlpha      *= fpc.uiAlphaMultiplier;

    // Alpha-over blend in linear domain
    vec3 blended = uiRGB * uiAlpha + fpc.bgLinear * (1.0 - uiAlpha);
    outColor = vec4(blended, 1.0);
}
