#version 450
#include "../inc/render_parameters.h"
#include "ocean.inc"

invariant gl_Position;

#if defined(VARIANT_BIT_0)
#define OCEAN_BORDER
#endif

#ifdef OCEAN_BORDER
layout(location = 0) in vec3 aPosition;
#else
layout(location = 0) in uvec4 aPosition;
layout(location = 1) in vec4 aLODWeights;
#endif

layout(location = 0) out vec3 vPos;
layout(location = 1) out vec4 vGradNormalUV;

#ifndef NO_HEIGHTMAP
layout(set = 2, binding = 0) uniform sampler2D uHeightmap;
#endif
layout(set = 2, binding = 1) uniform sampler2D uLodMap;

#ifndef OCEAN_BORDER
layout(set = 3, binding = 0, std140) uniform Patches
{
    PatchData data[512];
} patches;
#endif

layout(std430, push_constant) uniform Registers
{
    vec2 inv_heightmap_size;
    vec2 inv_ocean_grid_count;
    vec2 normal_uv_scale;
    vec2 integer_to_world_mod;
    vec2 heightmap_range;
    vec2 base_position;
} registers;

#ifndef OCEAN_BORDER
vec2 warp_position()
{
    float vlod = dot(aLODWeights, patches.data[gl_InstanceIndex].LODs);
    vlod = mix(vlod, patches.data[gl_InstanceIndex].InnerLOD, all(equal(aLODWeights, vec4(0.0))));

    float floor_lod = floor(vlod);
    float fract_lod = vlod - floor_lod;
    uint ufloor_lod = uint(floor_lod);

    uvec2 mask = (uvec2(1u) << uvec2(ufloor_lod, ufloor_lod + 1u)) - 1u;
    uvec4 rounding = aPosition.zwzw * mask.xxyy;
    vec4 lower_upper_snapped = vec4((aPosition.xyxy + rounding) & ~mask.xxyy);
    return mix(lower_upper_snapped.xy, lower_upper_snapped.zw, fract_lod);
}
#endif

mediump vec2 lod_factor(vec2 pos)
{
    mediump float level = textureLod(uLodMap, pos * registers.inv_ocean_grid_count, 0.0).x;
    mediump float floor_level = floor(level);
    mediump float fract_level = level - floor_level;
    return vec2(floor_level, fract_level);
}

#ifndef NO_HEIGHTMAP
mediump vec3 sample_height_displacement(vec2 uv, vec2 off, mediump vec2 lod)
{
    return clamp(mix(
            textureLod(uHeightmap, uv + 0.5 * off, lod.x).xyz,
            textureLod(uHeightmap, uv + 1.0 * off, lod.x + 1.0).xyz,
            lod.y), registers.heightmap_range.x, registers.heightmap_range.y);
}
#endif

void main()
{
#ifdef OCEAN_BORDER
    vec2 pos = aPosition.xy + registers.base_position;
#else
    vec2 pos = warp_position();
    pos += patches.data[gl_InstanceIndex].Offsets;
#endif
    mediump vec2 lod = lod_factor(pos);
    mediump float delta_mod = exp2(lod.x);

    vec2 uv = pos * registers.inv_heightmap_size;
    vec2 off = registers.inv_heightmap_size * delta_mod;

    vec2 centered_uv = uv + 0.5 * registers.inv_heightmap_size;
    vGradNormalUV = vec4(centered_uv, centered_uv * registers.normal_uv_scale);
#ifndef NO_HEIGHTMAP
    mediump vec3 height_displacement = sample_height_displacement(uv, off, lod).xyz;
#else
    mediump vec3 height_displacement = vec3(0.0);
#endif

#ifdef OCEAN_BORDER
    height_displacement *= aPosition.z;
#endif

    pos *= registers.integer_to_world_mod;
    pos += height_displacement.yz;
    vec3 world = vec3(pos.x, height_displacement.x, pos.y);
    vPos = world;
    gl_Position = global.view_projection * vec4(world, 1.0);
}
