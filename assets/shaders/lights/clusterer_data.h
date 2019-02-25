#ifndef CLUSTERER_DATA_H_
#define CLUSTERER_DATA_H_

#define CLUSTERER_MAX_LIGHTS 32

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

struct ClustererParameters
{
	mat4 transform;
	SpotShaderInfo spots[CLUSTERER_MAX_LIGHTS];
	PointShaderInfo points[CLUSTERER_MAX_LIGHTS];
	mat4 spot_shadow[CLUSTERER_MAX_LIGHTS];
	PointShadowData point_shadow[CLUSTERER_MAX_LIGHTS];
};

#endif
