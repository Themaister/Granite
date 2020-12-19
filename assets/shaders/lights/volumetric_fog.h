#ifndef VOLUMETRIC_FOG_H_
#define VOLUMETRIC_FOG_H_

#include "../inc/global_bindings.h"

float volumetric_fog_texture_z_to_world(float texture_z, float slice_z_log2_scale)
{
	float world_z = exp2(texture_z / slice_z_log2_scale) - 1.0;
	return world_z;
}

float volumetric_fog_world_to_texture_z(float world_z, float slice_z_log2_scale)
{
	mediump float texture_z = log2(1.0 + world_z) * slice_z_log2_scale;
	return texture_z;
}

mediump vec4 sample_volumetric_fog(sampler3D FogVolume, mediump vec2 uv, mediump float world_z, mediump float slice_z_log2_scale)
{
    mediump float texture_z = volumetric_fog_world_to_texture_z(world_z, slice_z_log2_scale);
    return textureLod(FogVolume, vec3(uv, texture_z), 0.0);
}

#if defined(RENDERER_FORWARD) && defined(VOLUMETRIC_FOG)
layout(set = 0, binding = BINDING_GLOBAL_VOLUMETRIC_FOG) uniform mediump sampler3D uFogVolume;
#endif

#endif