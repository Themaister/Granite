#version 450

#include "inc/render_parameters.h"

layout(location = 0) in vec3 Position;
layout(location = 1) in vec2 UV;
layout(location = 0) out highp vec2 vUV;

layout(push_constant, std430) uniform Registers
{
    vec3 color;
    float xz_scale;
} registers;

void main()
{
    vec3 pos = Position;
    pos.xz *= registers.xz_scale;

    gl_Position = global.view_projection * vec4(pos, 0.0);
    gl_Position.z = 0.0;
    vUV = UV;
}