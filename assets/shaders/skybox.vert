#version 450

#include "inc/render_parameters.h"

layout(location = 0) in vec2 Position;
layout(location = 0) out highp vec3 vDirection;

void main()
{
    gl_Position = vec4(Position, 1.0, 1.0);
    vDirection = (global.inv_local_view_projection * vec4(Position, 0.0, 1.0)).xyz;
}