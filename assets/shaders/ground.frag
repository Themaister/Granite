#version 450
precision highp float;
precision highp int;

#include "inc/render_target.h"
#include "inc/two_component_normal.h"

#if defined(VARIANT_BIT_0) && VARIANT_BIT_0
#define BANDLIMITED_PIXEL
#include "inc/bandlimited_pixel_filter.h"
const int bandlimited_pixel_lod = 0;
#endif

layout(location = 0) in highp vec3 vPos;

layout(push_constant, std430) uniform Constants
{
    mat4 Model;
    mat4 Normal;
} registers;

layout(location = 1) in highp vec2 vUV;

layout(set = 2, binding = 1) uniform mediump sampler2D uNormalsTerrain;
layout(set = 2, binding = 2) uniform mediump sampler2D uOcclusionTerrain;
layout(set = 2, binding = 4) uniform mediump sampler2DArray uBaseColor;
layout(set = 2, binding = 5) uniform mediump sampler2D uSplatMap;
layout(set = 2, binding = 6) uniform mediump sampler2D uDeepRoughNormals;

layout(std140, set = 3, binding = 1) uniform GroundData
{
    vec2 uInvHeightmapSize;
    vec2 uUVShift;
    vec2 uUVTilingScale;
    vec2 uTangentScale;
    vec4 uColorSize;
};

mediump float horiz_max(mediump vec4 v)
{
    mediump vec2 x = max(v.xy, v.zw);
    return max(x.x, x.y);
}

void main()
{
    highp vec2 uv = vUV * uUVTilingScale;

    mediump vec4 types = vec4(textureLod(uSplatMap, vUV, 0.0).rgb, 0.25);
    mediump float max_weight = horiz_max(types);
    types = types / max_weight;
    types = clamp(2.0 * (types - 0.5), vec4(0.0), vec4(1.0));
    mediump float weight = 1.0 / dot(types, vec4(1.0));
    types *= weight;

#ifdef BANDLIMITED_PIXEL
    BandlimitedPixelInfo info = compute_pixel_weights(uv, uColorSize.xy, uColorSize.zw, 1.0);
    mediump vec3 base_color =
        types.x * sample_bandlimited_pixel_array(uBaseColor, vec3(uv, 0.0), info, 0.0).rgb +
        types.y * sample_bandlimited_pixel_array(uBaseColor, vec3(uv, 1.0), info, 0.0).rgb +
        types.z * sample_bandlimited_pixel_array(uBaseColor, vec3(uv, 2.0), info, 0.0).rgb +
        types.w * sample_bandlimited_pixel_array(uBaseColor, vec3(uv, 3.0), info, 0.0).rgb;
#else
    mediump vec3 base_color =
        types.x * texture(uBaseColor, vec3(uv, 0.0)).rgb +
        types.y * texture(uBaseColor, vec3(uv, 1.0)).rgb +
        types.z * texture(uBaseColor, vec3(uv, 2.0)).rgb +
        types.w * texture(uBaseColor, vec3(uv, 3.0)).rgb;
#endif

    mediump vec3 terrain = two_component_normal(texture(uNormalsTerrain, vUV).xy * 2.0 - 1.0);
    terrain.xy += types.w * 0.5 * (texture(uDeepRoughNormals, uv).xy * 2.0 - 1.0);
    mediump vec3 normal = normalize(mat3(registers.Normal) * terrain.xzy); // Normal is +Y, Bitangent is +Z.

    emit_render_target(vec3(0.0), vec4(base_color, 1.0), normal, 0.0, 1.0, texture(uOcclusionTerrain, vUV).x, vPos);
}

