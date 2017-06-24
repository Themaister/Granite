#version 310 es
precision mediump float;

layout(location = 0) out vec4 FragColor;

layout(push_constant, std430) uniform Constants
{
    mat4 Model;
    mat4 Normal;
} registers;

layout(location = 0) in highp vec3 vWorld;
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
    types = clamp(4.0 * (types - 0.75), vec4(0.0), vec4(1.0));
    float weight = 1.0 / dot(types, vec4(1.0));
    types *= weight;

    vec3 base_color =
        types.x * texture(uBaseColor, vec3(uv, 0.0)).rgb +
        types.y * texture(uBaseColor, vec3(uv, 1.0)).rgb +
        types.z * texture(uBaseColor, vec3(uv, 2.0)).rgb +
        types.w * texture(uBaseColor, vec3(uv, 3.0)).rgb;

    float ndotl = clamp(dot(normal, normalize(vec3(1.0, 1.0, 1.0))), 0.0, 1.0);
    FragColor = vec4(base_color * (0.2 + 0.8 * ndotl), 1.0);
    //FragColor = vec4(0.5 * normal + 0.5, 1.0);
}

