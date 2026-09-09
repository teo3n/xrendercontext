#version 460

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 vColor;

layout(push_constant) uniform PushData {
    float angle;
    float aspect;
} pc;

void main() {
    const float s = sin(pc.angle);
    const float c = cos(pc.angle);
    vec2 rotated = vec2(inPos.x * c - inPos.y * s, inPos.x * s + inPos.y * c);
    rotated.x /= pc.aspect;

    vColor = inColor;
    gl_Position = vec4(rotated, 0.0, 1.0);
}
