#ifndef LIGHTING_DATA_H_
#define LIGHTING_DATA_H_

#include "../inc/global_bindings.h"

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

#define SHADOW_NUM_CASCADES 4
#define SHADOW_TRANSFORMS shadow.transforms
#define SHADOW_CASCADE_LOG_BIAS shadow.cascade_log_bias

struct ShadowParameters
{
	mat4 transforms[SHADOW_NUM_CASCADES];
	float cascade_log_bias;
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

layout(set = 0, binding = BINDING_GLOBAL_RENDER_PARAMETERS, std140) uniform LightingParameters
{
	FogParameters fog;
	EnvironmentParameters environment;
	ShadowParameters shadow;
	VolumetricFogParameters volumetric_fog;
	DirectionalParameters directional;
	RefractionParameters refraction;
	ResolutionParameters resolution;
};

#endif
