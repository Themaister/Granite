#ifndef LIGHTING_DATA_H_
#define LIGHTING_DATA_H_

struct EnvironmentParameters
{
	float intensity;
	float mipscale;
};

struct FogParameters
{
	vec3 color;
	float falloff;
};

struct VolumetricFogParameters
{
	float slice_z_log2_scale;
};

struct ShadowParameters
{
	mat4 near;
	mat4 far;
	float inv_cutoff_distance;
};

struct DirectionalParameters
{
	vec3 color;
	vec3 direction;
};

struct RefractionParameters
{
	vec3 falloff;
};

struct ResolutionParameters
{
	vec2 resolution;
	vec2 inv_resolution;
};

layout(set = 0, binding = 1, std140) uniform LightingParameters
{
	EnvironmentParameters environment;
	FogParameters fog;
	VolumetricFogParameters volumetric_fog;
	ShadowParameters shadow;
	DirectionalParameters directional;
	RefractionParameters refraction;
	ResolutionParameters resolution;
};

#endif
