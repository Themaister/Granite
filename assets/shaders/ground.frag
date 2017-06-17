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
layout(set = 2, binding = 3) uniform sampler2D uBaseColor;

layout(std140, set = 2, binding = 4) uniform GroundData
{
    vec2 uInvHeightmapSize;
    vec2 uUVShift;
    vec2 uUVTilingScale;
};

void main()
{
    vec3 terrain = texture(uNormalsTerrain, vUV).xyz * 2.0 - 1.0;
    vec3 normal = normalize(mat3(registers.Normal) * terrain.xzy); // Normal is +Y, Bitangent is +Z.

    vec2 uv = vUV * uUVTilingScale;
    vec4 base_color = texture(uBaseColor, uv, -1.5);
    FragColor = base_color;
}
