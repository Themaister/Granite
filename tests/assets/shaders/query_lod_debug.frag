#version 450

layout(set = 0, binding = 0) uniform sampler2D uSampler;
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 FragColor;

vec2 remap_range(vec2 v)
{
    return v * 0.1 + 0.5;
}

void main()
{
    FragColor = vec4(remap_range(textureLod(uSampler, vUV, 0.0).xy), 0.0, 1.0);
}