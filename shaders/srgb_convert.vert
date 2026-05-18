// Fullscreen triangle vertex shader (no vertex buffer needed)
// Triangle: (-1,-1), (3,-1), (-1,3) — covers entire [-1,1]×[-1,1] clip space

#version 450

layout(location = 0) out vec2 fragUV;

void main() {
    vec2 pos = vec2(
        (gl_VertexIndex == 1) ? 3.0 : -1.0,
        (gl_VertexIndex == 2) ? 3.0 : -1.0
    );
    gl_Position = vec4(pos, 0.0, 1.0);
    fragUV = pos * 0.5 + 0.5;  // [-1,3] → [0,2], restricted to [0,1] by viewport
}
