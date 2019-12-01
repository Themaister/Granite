#version 450

layout(set = 0, binding = 0) uniform sampler2D uSamplerLinearYUV420P;
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 FragColor;

void main()
{
    FragColor = textureLod(uSamplerLinearYUV420P, vUV, 0.0);
}