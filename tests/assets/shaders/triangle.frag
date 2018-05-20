#version 450

layout(constant_id = 0) const float R = 0.0;
layout(constant_id = 1) const float G = 0.0;
layout(constant_id = 2) const float B = 0.0;
layout(constant_id = 3) const float A = 0.0;

layout(location = 0) out vec4 FragColor;
layout(location = 0) in vec4 vColor;

void main()
{
    FragColor = vec4(R, G, B, A) * (vColor * 0.5 + 0.5);
}