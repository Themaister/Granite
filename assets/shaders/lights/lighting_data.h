#ifndef LIGHTING_DATA_H_
#define LIGHTING_DATA_H_

#ifdef ENVIRONMENT
layout(set = 0, binding = 1, std140) uniform EnvironmentData
{
	float intensity;
	float mipscale;
} environment;
#endif

#ifdef FOG
layout(set = 0, binding = 2, std140) uniform FogData
{
	vec3 color;
	float falloff;
} fog;
#endif

#ifdef VOLUMETRIC_FOG
layout(set = 0, binding = 2, std140) uniform FogData
{
	float slice_z_log2_scale;
} fog;
layout(set = 1, binding = 14) uniform mediump sampler3D uFogVolume;
#endif

#ifdef SHADOWS
layout(set = 0, binding = 3, std140) uniform ShadowData
{
	mat4 near;
	mat4 far;
	float inv_cutoff_distance;
} shadow;
#endif

layout(set = 0, binding = 4, std140) uniform DirectionalLight
{
	vec3 color;
	vec3 direction;
} directional;

#ifdef REFRACTION
layout(set = 0, binding = 5, std140) uniform RefractionData
{
	vec3 falloff;
} refraction;
#endif

layout(set = 0, binding = 6, std140) uniform ResolutionData
{
	vec2 resolution;
	vec2 inv_resolution;
} resolution;

#endif
