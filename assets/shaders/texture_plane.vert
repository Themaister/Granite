#version 310 es
#include "inc/render_parameters.h"

layout(location = 0) in vec2 Position;
layout(location = 0) out highp vec2 vUV;

layout(std430, push_constant) uniform Registers
{
    vec3 normal;
    vec3 tangent;
    vec3 bitangent;

    vec3 position;
    vec3 dPdx;
    vec3 dPdy;
} registers;

void main()
{
    vec3 plane = registers.position + Position.x * registers.dPdx + Position.y * registers.dPdy;
    gl_Position = global.view_projection * vec4(plane, 1.0);
    vUV = Position * 0.5 + 0.5;
}