#version 450
#include "inc/render_parameters.h"

layout(location = 0) in uvec4 aPosition;
layout(location = 1) in vec4 aLODWeights;

#ifndef RENDERER_DEPTH
layout(location = 0) out highp vec3 vPos;
layout(location = 1) out highp vec2 vUV;
#endif

layout(set = 2, binding = 0) uniform sampler2D uHeightmap;
layout(set = 2, binding = 3) uniform sampler2D uLodMap;

struct PatchData
{
    vec2 Offsets;
    float InnerLOD;
    float Padding;
    vec4 LODs;
};

layout(set = 3, binding = 0, std140) uniform Patches
{
    PatchData data[512];
} patches;

layout(std140, set = 3, binding = 1) uniform GroundData
{
    vec2 uInvHeightmapSize;
    vec2 uUVShift;
    vec2 uUVTilingScale;
    vec2 uTangentScale;
};

layout(push_constant, std430) uniform Constants
{
    mat4 Model;
    mat4 Normal;
} registers;

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
    return mix(lower_upper_snapped.xy, lower_upper_snapped.zw, fract_lod) + patches.data[gl_InstanceIndex].Offsets;
}

mediump vec2 lod_factor(vec2 uv)
{
    mediump float level = textureLod(uLodMap, uv, 0.0).x;
    mediump float floor_level = floor(level);
    mediump float fract_level = level - floor_level;
    return vec2(floor_level, fract_level);
}

mediump float sample_height_displacement(vec2 uv, vec2 off, mediump vec2 lod)
{
    return clamp(mix(
            textureLod(uHeightmap, uv + 0.5 * off, lod.x).x,
            textureLod(uHeightmap, uv + 1.0 * off, lod.x + 1.0).x,
            lod.y), -1.0, 1.0);
}

void main()
{
    vec2 pos = warp_position() * uInvHeightmapSize;
    vec2 uv = pos;
    mediump vec2 lod = lod_factor(uv);
    uv += uUVShift;

    mediump float delta_mod = exp2(lod.x);
    vec2 off = uInvHeightmapSize * delta_mod;

#ifndef RENDERER_DEPTH
    vUV = uv;
#endif
    float height_displacement = sample_height_displacement(uv, off, lod);

    vec4 world = registers.Model * vec4(pos.x, height_displacement, pos.y, 1.0);
#ifndef RENDERER_DEPTH
    vPos = world.xyz;
#endif
    gl_Position = global.view_projection * world;
}
