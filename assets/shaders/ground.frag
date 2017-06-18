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
layout(location = 2) in mediump float vLOD;

layout(set = 2, binding = 1) uniform sampler2D uNormalsTerrain;
layout(set = 2, binding = 3) uniform mediump sampler2DArray uBaseColor;
layout(set = 2, binding = 4) uniform sampler2D uTypeMap;

layout(std140, set = 3, binding = 1) uniform GroundData
{
    vec2 uInvHeightmapSize;
    vec2 uUVShift;
    vec2 uUVTilingScale;
};

float horiz_max(vec4 v)
{
    vec2 x = max(v.xy, v.zw);
    return max(x.x, x.y);
}

void main()
{
    vec3 terrain = texture(uNormalsTerrain, vUV).xyz * 2.0 - 1.0;
    vec3 normal = normalize(mat3(registers.Normal) * terrain.xzy); // Normal is +Y, Bitangent is +Z.

    vec4 types = textureLod(uTypeMap, vUV, 0.0);
    float max_weight = horiz_max(types);
    types = types / max_weight;
    types = clamp(5.0 * (types - 0.8), vec4(0.0), vec4(1.0));
    float weight = 1.0 / dot(types, vec4(1.0));
    types *= weight;

    vec2 uv = vUV * uUVTilingScale;

    vec3 base_color =
        types.x * texture(uBaseColor, vec3(uv, 0.0), -1.5).rgb +
        types.y * texture(uBaseColor, vec3(uv, 1.0), -1.5).rgb +
        types.z * texture(uBaseColor, vec3(uv, 2.0), -1.5).rgb +
        types.w * texture(uBaseColor, vec3(uv, 3.0), -1.5).rgb;

    FragColor = vec4(base_color, 1.0);
}
