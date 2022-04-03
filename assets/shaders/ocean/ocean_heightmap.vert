#ifdef OCEAN_BORDER
layout(location = 0) in vec3 aPosition;
#else
layout(location = 0) in uvec4 aPosition;
layout(location = 1) in vec4 aLODWeights;
#endif

layout(set = 2, binding = 0) uniform sampler2D uHeightmap;
layout(set = 2, binding = 1) uniform sampler2D uLodMap;

#ifndef OCEAN_BORDER
layout(set = 3, binding = 0, std140) readonly buffer Patches
{
    PatchData data[];
} patches;
#endif

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
    // Don't need precise here since X dimensions and Y dimensions are evaluated in same direction,
    // i.e. we will end up with exact same inputs.
    return mix(lower_upper_snapped.xy, lower_upper_snapped.zw, fract_lod);
}
#endif

mediump float lod_factor(vec2 pos)
{
    mediump float level = textureLod(uLodMap, pos * registers.inv_ocean_grid_count, 0.0).x;
    return level;
}

mediump vec3 sample_height_displacement(vec2 uv, mediump float lod)
{
    return clamp(textureLod(uHeightmap, uv, lod).xyz, registers.heightmap_range.x, registers.heightmap_range.y);
}

void main()
{
    // To avoid cracks, ensure that all edges are computed with integer coordinates.
    // This ensures no rounding errors, then we can offset with non-integer values.
    // Need precise here to avoid any reordering of expressions.
    // To neighboring patches will for example render overlapping vertex at:
    // warp_position = 128, offset = 0 and
    // warp_position = 0, offset = 128.
#ifdef OCEAN_BORDER
    precise vec2 integer_pos = aPosition.xy + registers.coord_offset;
#else
    vec2 warped_pos = warp_position();
    precise vec2 integer_pos = warped_pos + patches.data[gl_InstanceIndex].Offsets;
#endif

    mediump float lod = lod_factor(integer_pos);

    vec2 uv = integer_pos * registers.inv_heightmap_size;
    vec2 centered_uv = uv + 0.5 * registers.inv_heightmap_size;
    vGradNormalUV = vec4(centered_uv, centered_uv * registers.normal_uv_scale);
    mediump vec3 height_displacement = sample_height_displacement(centered_uv, lod).xyz;

#ifdef OCEAN_BORDER
    height_displacement *= aPosition.z;
#endif

    vec2 world_pos = integer_pos * registers.integer_to_world_mod;
    world_pos += height_displacement.yz;
    vec3 world = vec3(world_pos.x, height_displacement.x, world_pos.y) + registers.world_offset;
    vPos = world;
    gl_Position = global.view_projection * vec4(world, 1.0);
}
