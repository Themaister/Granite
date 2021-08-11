#ifndef CLUSTERER_DATA_H_
#define CLUSTERER_DATA_H_

#define CLUSTERER_MAX_LIGHTS_GLOBAL 32
#ifdef CLUSTERER_BINDLESS
#define CLUSTERER_MAX_LIGHTS 4096
#define CLUSTERER_MAX_DECALS 4096
#else
#define CLUSTERER_MAX_LIGHTS CLUSTERER_MAX_LIGHTS_GLOBAL
#endif

struct PositionalLightInfo
{
	mediump vec3 color;
	uint spot_scale_bias;
	vec3 position;
	uint offset_radius;
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
	int num_decals;
	int num_decals_32;
	int decals_texture_offset;
	int z_max_index;
	float z_scale;
};

struct BindlessDecalTransform
{
	vec4 world_to_texture[3];
};

struct ClustererBindlessTransforms
{
	PositionalLightInfo lights[CLUSTERER_MAX_LIGHTS];
	mat4 shadow[CLUSTERER_MAX_LIGHTS];
	mat4 model[CLUSTERER_MAX_LIGHTS];
	uint type_mask[CLUSTERER_MAX_LIGHTS / 32];
	BindlessDecalTransform decals[CLUSTERER_MAX_DECALS];
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

struct ClustererGlobalTransforms
{
	PositionalLightInfo lights[CLUSTERER_MAX_LIGHTS_GLOBAL];
	mat4 shadow[CLUSTERER_MAX_LIGHTS_GLOBAL];
	uint type_mask;
	int desc_offset;
	int num_lights;
};

#endif
