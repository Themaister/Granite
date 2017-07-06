#version 310 es
precision mediump float;

#include "inc/fog.h"

layout(location = 0) out vec3 Emissive;
layout(location = 1) out vec4 BaseColor;
layout(location = 2) out vec3 Normal;
layout(location = 3) out vec2 PBR;

layout(location = 0) in mediump vec3 vEyeVec;

layout(push_constant, std430) uniform Constants
{
    mat4 Model;
    mat4 Normal;
} registers;

layout(location = 1) in highp vec2 vUV;

layout(set = 2, binding = 1) uniform sampler2D uNormalsTerrain;
layout(set = 2, binding = 3) uniform mediump sampler2DArray uBaseColor;
layout(set = 2, binding = 4) uniform sampler2D uSplatMap;

layout(std140, set = 3, binding = 1) uniform GroundData
{
    vec2 uInvHeightmapSize;
    vec2 uUVShift;
    vec2 uUVTilingScale;
    vec2 uTangentScale;
};

float horiz_max(vec4 v)
{
    vec2 x = max(v.xy, v.zw);
    return max(x.x, x.y);
}

void main()
{
    highp vec2 uv = vUV * uUVTilingScale;

    vec3 terrain = texture(uNormalsTerrain, vUV).xyz * 2.0 - 1.0;
    vec3 normal = normalize(mat3(registers.Normal) * terrain.xzy); // Normal is +Y, Bitangent is +Z.

    vec4 types = vec4(textureLod(uSplatMap, vUV, 0.0).rgb, 0.25);
    float max_weight = horiz_max(types);
    types = types / max_weight;
    types = clamp(2.0 * (types - 0.5), vec4(0.0), vec4(1.0));
    float weight = 1.0 / dot(types, vec4(1.0));
    types *= weight;

    const float lod = 1.0;
    vec3 base_color =
        types.x * texture(uBaseColor, vec3(uv, 0.0), lod).rgb +
        types.y * texture(uBaseColor, vec3(uv, 1.0), lod).rgb +
        types.z * texture(uBaseColor, vec3(uv, 2.0), lod).rgb +
        types.w * texture(uBaseColor, vec3(uv, 3.0), lod).rgb;


    Emissive = vec3(0.0);
    BaseColor = vec4(base_color, 1.0);
    Normal = normal * 0.5 + 0.5;
    PBR = vec2(0.0, 1.0);
}

