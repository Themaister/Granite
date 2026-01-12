#ifndef MESHLET_UBOS_H_
#define MESHLET_UBOS_H_

layout(set = 3, binding = 0, std140) uniform Frustum
{
	vec4 viewport;
	vec4 planes[6];
	mat4 view;
	vec4 viewport_scale_bias;
	ivec2 hiz_resolution;
	float winding;
	int hiz_min_lod;
	int hiz_max_lod;
} frustum;

#endif