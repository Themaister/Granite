#version 450

#ifdef VOLUMETRIC_FOG
#include "lights/lighting_data.h"
#include "inc/render_parameters.h"
#include "lights/volumetric_fog.h"
#endif

layout(location = 0) out mediump vec3 Emissive;
layout(location = 0) in highp vec2 vUV;
layout(set = 2, binding = 0) uniform mediump sampler2D uCylinder;

layout(push_constant, std430) uniform Registers
{
    vec3 color;
    float xz_scale;
} registers;

void main()
{
    Emissive = texture(uCylinder, vUV).rgb * registers.color;
#ifdef VOLUMETRIC_FOG
    mediump vec4 fog = sample_volumetric_fog(uFogVolume,
        gl_FragCoord.xy * resolution.inv_resolution,
        10.0 * global.z_far,
        volumetric_fog.slice_z_log2_scale);
    Emissive = fog.rgb + Emissive * fog.a;
#endif
}