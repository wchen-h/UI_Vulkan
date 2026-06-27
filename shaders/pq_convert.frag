// HDR convert: passthrough (identity)
// hdr_ui.frag already does mixing + BT.2020 + PQ + YCbCr adjustment
// This shader just copies the linear intermediate to the swapchain

#version 450

layout(binding = 0) uniform sampler2D texLinear;

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(texture(texLinear, fragUV).rgb, 1.0);
}
