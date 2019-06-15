#version 450

layout(location = 0) out vec2 FragColor;
layout(set = 0, binding = 0) uniform sampler2D uSampler;
layout(location = 0) in vec2 vUV;

void main()
{
    FragColor = textureQueryLod(uSampler, vUV * 0.0);
}
