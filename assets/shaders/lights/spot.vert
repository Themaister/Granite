#version 450

#include "../inc/render_parameters.h"
#include "../inc/affine.h"

layout(std140, set = 2, binding = 1) uniform Parameters
{
#if defined(VARIANT_BIT_2)
    mat_affine transforms[128];
#else
    mat_affine transform;
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
        vec3 world = mul(transforms[gl_InstanceIndex], vec4(Position));
    #else
        vec3 world = mul(transform, vec4(Position));
    #endif
    vec4 clip = global.view_projection * vec4(world, 1.0);
    gl_Position = clip;
#endif

#if defined(VARIANT_BIT_2) && !defined(RENDERER_DEPTH)
    vIndex = gl_InstanceIndex;
#endif
}
