#version 310 es
precision mediump float;

layout(set = 2, binding = 0) uniform samplerCube uCube;
layout(location = 0) out vec4 FragColor;
layout(location = 0) in highp vec3 vDirection;

layout(push_constant, std430) uniform Registers
{
    float lod;
} registers;

#define PI 3.1415628

// Placeholder

void main()
{
    FragColor = textureLod(uCube, vDirection, registers.lod + 1.0);
}