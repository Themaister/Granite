#version 450

layout(set = 2, binding = 0) uniform sampler2D uTex;
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 FragColor;

void main()
{
    FragColor = texture(uTex, vUV);
}