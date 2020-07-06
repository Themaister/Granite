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
    // Work around case where zw = 0.0, which freaks out any clipper.
    gl_Position.w = (gl_Position.w >= 0.0 ? 1.0 : -1.0) * max(abs(gl_Position.w), 0.00001);
    gl_Position.z = 0.99999 * gl_Position.w;
    vUV = UV;
}
