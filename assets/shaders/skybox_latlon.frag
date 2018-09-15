#version 450

#ifdef VOLUMETRIC_FOG
#include "lights/lighting_data.h"
#include "inc/render_parameters.h"
#include "lights/volumetric_fog.h"
#endif

#if defined(HAVE_EMISSIVE) && HAVE_EMISSIVE
layout(set = 2, binding = 0) uniform mediump sampler2D uSkybox;
#endif
layout(location = 0) in highp vec3 vDirection;

layout(location = 0) out mediump vec3 Emissive;

layout(std430, push_constant) uniform Registers
{
    vec3 color;
} registers;

void main()
{
#if defined(HAVE_EMISSIVE) && HAVE_EMISSIVE
    vec3 v = normalize(vDirection);
    if (abs(v.x) < 0.00001)
        v.x = 0.00001;

    vec2 uv = vec2(atan(v.z, v.x), asin(-v.y));
    uv *= vec2(0.1591, 0.3183);
    uv += 0.5;
    Emissive = textureLod(uSkybox, uv, 0.0).rgb * registers.color;
#else
    Emissive = registers.color;
#endif

#ifdef VOLUMETRIC_FOG
    mediump vec4 fog = sample_volumetric_fog(uFogVolume,
        gl_FragCoord.xy * resolution.inv_resolution,
        10.0 * global.z_far,
        volumetric_fog.slice_z_log2_scale);
    Emissive = fog.rgb + Emissive * fog.a;
#endif
}
