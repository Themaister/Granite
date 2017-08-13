#version 310 es
precision mediump float;

layout(location = 0) out vec3 Emissive;
layout(location = 0) in highp vec2 vUV;
layout(set = 2, binding = 0) uniform sampler2D uCylinder;

layout(push_constant, std430) uniform Registers
{
    vec3 color;
    float xz_scale;
} registers;

void main()
{
    Emissive = texture(uCylinder, vUV).rgb * registers.color;
}