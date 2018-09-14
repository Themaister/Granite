#version 450

#include "../inc/render_parameters.h"

layout(std140, set = 2, binding = 1) uniform Parameters
{
#if defined(VARIANT_BIT_2)
    mat4 transforms[128];
#else
    mat4 transform;
#endif
};

layout(location = 0) in vec4 Position;

#if defined(VARIANT_BIT_2) && !defined(RENDERER_DEPTH)
layout(location = 0) flat out int vIndex;
#endif

void main()
{
#if defined(VARIANT_BIT_0)
    gl_Position = vec4(Position.xy, 1.0, 1.0);
#else
    #if defined(VARIANT_BIT_2)
        vec4 world = transforms[gl_InstanceIndex] * vec4(Position);
    #else
        vec4 world = transform * vec4(Position);
    #endif
    vec4 clip = global.view_projection * world;
    gl_Position = clip;
#endif

#if defined(VARIANT_BIT_2) && !defined(RENDERER_DEPTH)
    vIndex = gl_InstanceIndex;
#endif
}
