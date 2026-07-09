// SDR merged shader: UI mixing (BT.709 linear) + sRGB encode
// Full-screen quad: bg fills entire window, UI drawn on top in centered area

#version 450

layout(binding = 0) uniform sampler2D texRGB;    // sRGB texture -> hardware decodes to linear
layout(binding = 1) uniform sampler2D texAlpha;  // linear alpha texture
layout(binding = 2) uniform sampler2D texBG;     // background image (sRGB -> linear BT.709)

layout(push_constant) uniform FragPush {
    // bytes 0-15: vertex (offset + scale) — full-screen: offset=(0,0), scale=(2,2)
    layout(offset = 16) float alpha;         // sdrAlpha (UI opacity multiplier)
    layout(offset = 20) float bgMultiplier;  // background brightness multiplier
    layout(offset = 24) float yScale;        // unused in SDR (kept for layout compat)
    layout(offset = 28) float cbcrScale;     // unused in SDR (kept for layout compat)
    layout(offset = 32) vec2  uiOffset;      // UI area bottom-left in screen UV [0,1]
    layout(offset = 40) vec2  uiScale;       // UI area size in screen UV [0,1]
} fpc;

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

void main() {
    // 1. Sample background (sRGB -> linear, stretched to fill screen)
    vec3 bgRGB = texture(texBG, fragUV).rgb;
    vec3 bg    = bgRGB * fpc.bgMultiplier;

    // 2. Compute UI UV from full-screen UV
    vec2 uiUV = (fragUV - fpc.uiOffset) / fpc.uiScale;
    bool insideUI = (uiUV.x >= 0.0 && uiUV.x <= 1.0 &&
                     uiUV.y >= 0.0 && uiUV.y <= 1.0);

    if (!insideUI) {
        outColor = vec4(bg, 1.0);
        return;
    }

    // 3. Sample UI (sRGB -> linear, [0,1])
    vec3  uiRGB    = texture(texRGB, uiUV).rgb;
    float texAlpha = texture(texAlpha, uiUV).r;

    float effAlpha = texAlpha * fpc.alpha;
    vec3  blended  = uiRGB * effAlpha + bg * (1.0 - effAlpha);

    outColor = vec4(blended, 1.0);
}
