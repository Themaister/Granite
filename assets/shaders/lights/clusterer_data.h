#ifndef CLUSTERER_DATA_H_
#define CLUSTERER_DATA_H_

#ifdef CLUSTERER_BINDLESS
#define CLUSTERER_MAX_LIGHTS 4096
#else
#define CLUSTERER_MAX_LIGHTS 32
#endif

struct SpotShaderInfo
{
	mediump vec3 color;
	mediump float spot_scale;

	vec3 position;
	mediump float spot_bias;

	mediump vec3 direction;
	mediump float inv_radius;
};

struct PointShaderInfo
{
	mediump vec3 color;
	vec3 position;
	mediump vec3 direction;
	mediump float inv_radius;
};

struct PointShadowData
{
	vec4 transform;
	vec4 slice;
};

#ifdef CLUSTERER_BINDLESS
struct ClustererParametersBindless
{
	vec2 xy_scale;
	ivec2 resolution_xy;

	uint num_lights;
	uint num_lights_32;
	uint z_resolution;
	float inv_z_scale;
};

struct ClustererBindlessTransforms
{
	SpotShaderInfo spots[CLUSTERER_MAX_LIGHTS];
	PointShaderInfo points[CLUSTERER_MAX_LIGHTS];
	mat4 spot_shadow[CLUSTERER_MAX_LIGHTS];
	PointShadowData point_shadow[CLUSTERER_MAX_LIGHTS];
	uint type_mask[CLUSTERER_MAX_LIGHTS / 32];
};
#else
struct ClustererParametersLegacy
{
	mat4 transform;
	SpotShaderInfo spots[CLUSTERER_MAX_LIGHTS];
	PointShaderInfo points[CLUSTERER_MAX_LIGHTS];
	mat4 spot_shadow[CLUSTERER_MAX_LIGHTS];
	PointShadowData point_shadow[CLUSTERER_MAX_LIGHTS];
};
#endif

#endif
