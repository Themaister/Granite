#version 450

#include "../inc/srgb.h"
#include "lanczos2.h"

layout(location = 0) out vec4 FragColor;
layout(location = 0) in vec2 vUV;
layout(set = 0, binding = 0) uniform sampler2D uTex;

layout(set = 1, binding = 0) uniform Registers
{
    vec2 resolution;
    vec2 inv_resolution;
    vec2 out_resolution;
    vec2 inv_out_resolution;
} registers;

void main()
{
    vec2 coord = vUV * registers.resolution;
    vec3 color = lanczos2(uTex, coord, registers.inv_resolution);

#if TARGET_SRGB
    color = decode_srgb(color);
#endif

    FragColor = vec4(color, 1.0);
}