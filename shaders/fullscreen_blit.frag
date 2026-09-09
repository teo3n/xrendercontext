#version 460

layout(binding = 0) uniform sampler2D uImage;

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

void main()
{
    outColor = texture(uImage, inUV);
}
