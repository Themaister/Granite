#version 450

#ifdef VOLUMETRIC_FOG
#include "lights/lighting_data.h"
#include "inc/render_parameters.h"
#include "lights/volumetric_fog.h"
#endif

#if defined(HAVE_EMISSIVE) && HAVE_EMISSIVE
layout(set = 2, binding = 0) uniform mediump samplerCube uSkybox;
#endif
layout(location = 0) in highp vec3 vDirection;

layout(location = 0) out mediump vec3 Emissive;

layout(std430, push_constant) uniform Registers
{
    vec3 color;
    float camera_height;
    vec3 sun_direction;
} registers;

#if !defined(HAVE_EMISSIVE) || !HAVE_EMISSIVE
#include "lights/atmospheric_scatter.h"
#endif

void main()
{
#if defined(HAVE_EMISSIVE) && HAVE_EMISSIVE
    Emissive = texture(uSkybox, vDirection).rgb * registers.color;
#else
    Emissive = registers.color * rayleigh_mie_scatter(normalize(vDirection),
        registers.sun_direction, registers.camera_height,
        16, 8);
#endif
#ifdef VOLUMETRIC_FOG
    mediump vec4 fog = sample_volumetric_fog(uFogVolume,
        gl_FragCoord.xy * resolution.inv_resolution,
        10.0 * global.z_far,
        volumetric_fog.slice_z_log2_scale);
    Emissive = fog.rgb + Emissive * fog.a;
#endif
}
