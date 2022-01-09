#version 450
#include "../inc/render_parameters.h"
#include "ocean.inc"

invariant gl_Position;

#if defined(VARIANT_BIT_0)
#define OCEAN_BORDER
#endif

#if defined(VARIANT_BIT_3)
#define NO_HEIGHTMAP
#endif

layout(location = 0) out vec3 vPos;
layout(location = 1) out vec4 vGradNormalUV;

layout(std140, set = 2, binding = 6) uniform Registers
{
    vec3 world_offset;
    vec2 coord_offset;

    vec2 inv_heightmap_size;
    vec2 inv_ocean_grid_count;
    vec2 normal_uv_scale;
    vec2 integer_to_world_mod;
    vec2 heightmap_range;
} registers;

#ifdef NO_HEIGHTMAP
#include "ocean_plane.vert"
#else
#include "ocean_heightmap.vert"
#endif
