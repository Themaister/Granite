#ifndef LIGHTING_DATA_H_
#define LIGHTING_DATA_H_

layout(set = 0, binding = 1, std140) uniform EnvironmentData
{
	float intensity;
	float mipscale;
} environment;

layout(set = 0, binding = 2, std140) uniform FogData
{
	vec3 color;
	float falloff;
} fog;

layout(set = 0, binding = 3, std140) uniform ShadowData
{
	mat4 near;
	mat4 far;
	float inv_cutoff_distance;
} shadow;

layout(set = 0, binding = 4, std140) uniform DirectionalLight
{
	vec3 color;
	vec3 direction;
} directional;

#endif
