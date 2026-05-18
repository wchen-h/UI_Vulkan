// UI quad vertex shader
// Transforms a unit quad to screen-space rectangle centered in window

#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUV;

layout(push_constant) uniform PushConstants {
    vec2 offset;    // bottom-left corner in NDC
    vec2 scale;     // quad size in NDC
} pc;

layout(location = 0) out vec2 fragUV;

void main() {
    // scale + offset to position quad
    gl_Position = vec4(inPosition * pc.scale + pc.offset, 0.0, 1.0);
    fragUV = inUV;
}
