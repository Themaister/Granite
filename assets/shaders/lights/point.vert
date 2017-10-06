#version 450

#include "../inc/render_parameters.h"

layout(std140, set = 2, binding = 1) uniform Parameters
{
    mat4 transforms[256];
};

layout(location = 0) in vec4 Position;
layout(location = 0) flat out int vIndex;

void main()
{
#if VARIANT_ID == 1
    gl_Position = vec4(Position.xy, 1.0, 1.0);
#elif VARIANT_ID == 0
    const float fudge_factor = 1.15; // Make sure cover the entire volume.
    vec4 world = transforms[gl_InstanceIndex] * (vec4(fudge_factor, fudge_factor, fudge_factor, 1.0) * vec4(Position));
    vec4 clip = global.view_projection * world;
    gl_Position = clip;
#endif

    vIndex = gl_InstanceIndex;
}
