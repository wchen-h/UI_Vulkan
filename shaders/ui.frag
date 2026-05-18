// UI fragment shader (render to linear intermediate)
// Blends UI RGB + alpha over 18% gray background in LINEAR domain

#version 450

layout(binding = 0) uniform sampler2D texRGB;    // sRGB texture → hardware decodes to linear
layout(binding = 1) uniform sampler2D texAlpha;  // linear alpha texture

layout(push_constant) uniform FragPush {
    layout(offset = 16) float uiAlphaMultiplier;  // bytes 16-19, after vertex offset+scale
} fpc;

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

const float backgroundGray = 0.18;  // linear 18% gray

void main() {
    vec3 uiRGB   = texture(texRGB, fragUV).rgb;     // hardware sRGB→linear
    float uiAlpha = texture(texAlpha, fragUV).r;     // linear, independent of sRGB
    uiAlpha *= fpc.uiAlphaMultiplier;

    // Alpha-over blend in linear domain
    vec3 blended = uiRGB * uiAlpha + vec3(backgroundGray) * (1.0 - uiAlpha);
    outColor = vec4(blended, 1.0);
}
