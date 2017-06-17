#version 310 es
precision mediump float;

layout(location = 0) out vec4 FragColor;

layout(push_constant, std430) uniform Constants
{
    mat4 Model;
    mat3 Normal;
} registers;

layout(location = 0) in highp vec3 vWorld;
layout(location = 1) in highp vec2 vUV;

layout(set = 2, binding = 1) uniform sampler2D uNormalsTerrain;

layout(std140, set = 2, binding = 4) uniform GroundData
{
    vec2 uInvHeightmapSize;
    vec2 uUVShift;
    vec2 uUVHalfTexel;
    vec2 uUVTilingScale;
};

void main()
{
    vec3 terrain = texture(uNormalsTerrain, vUV).xyz * 2.0 - 1.0;
    vec3 normal = normalize(registers.Normal * terrain.xzy); // Normal is +Y, Bitangent is +Z.
    FragColor = vec4(0.5 * normal + 0.5, 1.0);
}