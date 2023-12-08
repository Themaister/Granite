#version 450

layout(location = 0) in mediump vec3 vNormal;
layout(location = 1) in mediump vec4 vTangent;
layout(location = 2) in vec2 vUV;

layout(location = 0) out vec4 FragColor;

void main()
{
    FragColor = vec4(vNormal.xyz * 0.5 + 0.5, 1.0);
}
