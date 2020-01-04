#ifndef CLUSTERER_DATA_H_
#define CLUSTERER_DATA_H_

#ifdef CLUSTERER_BINDLESS
#define CLUSTERER_MAX_LIGHTS 4096
#else
#define CLUSTERER_MAX_LIGHTS 32
#endif

struct PositionalLightInfo
{
	mediump vec3 color;
	mediump float spot_scale;
	vec3 position;
	mediump float spot_bias;
	mediump vec3 direction;
	mediump float inv_radius;
};

#ifdef CLUSTERER_BINDLESS
struct ClustererParametersBindless
{
	mat4 transform;
	vec4 clip_scale;

	vec3 camera_base;
	vec3 camera_front;

	vec2 xy_scale;
	ivec2 resolution_xy;
	vec2 inv_resolution_xy;

	int num_lights;
	int num_lights_32;
	int z_max_index;
	float z_scale;
};

struct ClustererBindlessTransforms
{
	PositionalLightInfo lights[CLUSTERER_MAX_LIGHTS];
	mat4 shadow[CLUSTERER_MAX_LIGHTS];
	mat4 model[CLUSTERER_MAX_LIGHTS];
	uint type_mask[CLUSTERER_MAX_LIGHTS / 32];
};
#else
struct PointShadowData
{
	vec4 transform;
	vec4 slice;
};

struct ClustererParametersLegacy
{
	mat4 transform;
	PositionalLightInfo spots[CLUSTERER_MAX_LIGHTS];
	PositionalLightInfo points[CLUSTERER_MAX_LIGHTS];
	mat4 spot_shadow[CLUSTERER_MAX_LIGHTS];
	PointShadowData point_shadow[CLUSTERER_MAX_LIGHTS];
};
#endif

#endif
