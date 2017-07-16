#version 310 es
precision mediump float;

layout(location = 0) in highp vec2 vUV;

layout(location = 0) out vec3 Emissive;
layout(location = 1) out vec4 BaseColor;
layout(location = 2) out vec3 Normal;
layout(location = 3) out vec2 PBR;

layout(set = 2, binding = 0) uniform sampler2D uReflection;

layout(std430, push_constant) uniform Registers
{
    vec3 normal;
    vec3 tangent;
    vec3 bitangent;

    vec3 position;
    vec3 dPdx;
    vec3 dPdy;
} registers;

void main()
{
    Emissive = texture(uReflection, vUV).rgb;
    Normal = registers.normal * 0.5 + 0.5;
    BaseColor = vec4(0.02, 0.02, 0.02, 1.0);
    PBR = vec2(1.0, 1.0); // No diffuse, no specular, only reflection.
}
