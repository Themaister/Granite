#version 450
precision highp float;
precision highp int;

#define VOLUMETRIC_FOG_SET 2
#define VOLUMETRIC_FOG_BINDING 0
#include "volumetric_fog.h"

layout(std430, push_constant) uniform Registers
{
    vec4 inverse_z;
    float slice_z_log2_scale;
} registers;

layout(input_attachment_index = 3, set = 3, binding = 3) uniform highp subpassInput Depth;
layout(location = 0) out mediump vec4 FragColor;
layout(location = 0) in vec2 vUV;
layout(set = 2, binding = 0) uniform mediump sampler3D uFogVolume;

float to_world_depth(float z)
{
    vec2 zw = z * registers.inverse_z.xy + registers.inverse_z.zw;
    return -zw.x / zw.y;
}

void main()
{
    float depth = subpassLoad(Depth).x;
    float world_depth = to_world_depth(depth);
    mediump vec4 fog = sample_volumetric_fog(uFogVolume, vUV, world_depth, registers.slice_z_log2_scale);
    FragColor = fog; // RGB additive fog (in-scatter), A (out-scatter for scene). Use blending.
}