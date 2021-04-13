#version 450

#include "inc/render_parameters.h"

layout(location = 0) in vec3 Position;
layout(location = 0) out vec3 vNormal;

layout(set = 3, binding = 0) uniform UBO
{
    vec3 pos;
    float radius;
    vec3 tex_coord;
};

void main()
{
    vec3 World = Position * radius + pos;
    gl_Position = global.view_projection * vec4(World, 1.0);
    vNormal = Position;
}
