#ifndef MESHLET_RENDER_H_
#define MESHLET_RENDER_H_

#ifndef MESHLET_RENDER_DESCRIPTOR_SET
#error "Must define MESHLET_RENDER_DESCRIPTOR_SET before including meshlet_render.h"
#endif

#ifndef MESHLET_RENDER_AABB_BINDING
#error "Must define MESHLET_RENDER_AABB_BINDING before including meshlet_render.h"
#endif

#ifndef MESHLET_RENDER_TRANSFORM_BINDING
#error "Must define MESHLET_RENDER_TRANSFORM_BINDING before including meshlet_render.h"
#endif

#ifndef MESHLET_RENDER_BOUND_BINDING
#error "Must define MESHLET_RENDER_BOUND_BINDING before including meshlet_render.h"
#endif

#ifndef MESHLET_RENDER_FRUSTUM_BINDING
#error "Must define MESHLET_RENDER_GROUP_BOUND_BINDING before including meshlet_render.h"
#endif

#ifndef MESHLET_RENDER_TASKS_BINDING
#error "Must define MESHLET_RENDER_TASKS_BINDING before including meshlet_render.h"
#endif

#include "meshlet_render_types.h"

layout(set = MESHLET_RENDER_DESCRIPTOR_SET, binding = MESHLET_RENDER_BOUND_BINDING, std430) readonly buffer Bounds
{
	Bound data[];
} bounds;

layout(set = MESHLET_RENDER_DESCRIPTOR_SET, binding = MESHLET_RENDER_AABB_BINDING, std430) readonly buffer AABBSSBO
{
	AABB data[];
} aabb;

layout(set = MESHLET_RENDER_DESCRIPTOR_SET, binding = MESHLET_RENDER_TRANSFORM_BINDING, std430) readonly buffer Transforms
{
	mat4 data[];
} transforms;

#ifdef MESHLET_RENDER_HIZ_BINDING
layout(set = MESHLET_RENDER_DESCRIPTOR_SET, binding = MESHLET_RENDER_HIZ_BINDING)
uniform texture2D uHiZDepth;
#endif

layout(set = MESHLET_RENDER_DESCRIPTOR_SET, binding = MESHLET_RENDER_FRUSTUM_BINDING, std140) uniform Frustum
{
	vec4 planes[6];
	mat4 view;
	vec4 viewport_scale_bias;
	ivec2 hiz_resolution;
	int hiz_max_lod;
} frustum;

layout(set = MESHLET_RENDER_DESCRIPTOR_SET, binding = MESHLET_RENDER_TASKS_BINDING, std430) readonly buffer Tasks
{
	TaskInfo data[];
} task_info;

#ifdef MESHLET_RENDER_OCCLUDER_BINDING
layout(set = MESHLET_RENDER_DESCRIPTOR_SET, binding = MESHLET_RENDER_OCCLUDER_BINDING, std430) buffer OccluderState
{
	uint data[];
} occluders;
#endif

bool frustum_cull(vec3 lo, vec3 hi)
{
	bool ret = true;
	for (int i = 0; i < 6 && ret; i++)
	{
		vec4 p = frustum.planes[i];
		bvec3 high_mask = greaterThan(p.xyz, vec3(0.0));
		vec3 max_coord = mix(lo, hi, high_mask);
		if (dot(vec4(max_coord, 1.0), p) < 0.0)
			ret = false;
	}
	return ret;
}

vec3 view_transform_yz_flip(vec3 pos)
{
	vec3 view = (frustum.view * vec4(pos, 1.0)).xyz;
	// Rearrange -Z to +Z.
	// Apply Y flip here.
	view.yz = -view.yz;
	return view;
}

#ifdef MESHLET_RENDER_HIZ_BINDING
bool hiz_cull(vec2 view_range_x, vec2 view_range_y, float closest_z)
{
	// Viewport scale first applies any projection scale in X/Y (without Y flip).
	// The scale also does viewport size / 2 and then offsets into integer window coordinates.
	vec2 range_x = view_range_x * frustum.viewport_scale_bias.x + frustum.viewport_scale_bias.z;
	vec2 range_y = view_range_y * frustum.viewport_scale_bias.y + frustum.viewport_scale_bias.w;

	ivec2 ix = ivec2(range_x);
	ivec2 iy = ivec2(range_y);

	ix.x = clamp(ix.x, 0, frustum.hiz_resolution.x - 1);
	ix.y = clamp(ix.y, ix.x, frustum.hiz_resolution.x - 1);
	iy.x = clamp(iy.x, 0, frustum.hiz_resolution.y - 1);
	iy.y = clamp(iy.y, iy.x, frustum.hiz_resolution.y - 1);

	// We need to sample from a LOD where where there is at most one texel delta
	// between lo/hi values.
	int max_delta = max(ix.y - ix.x, iy.y - iy.x);
	int lod = min(findMSB(max_delta - 1) + 1, frustum.hiz_max_lod);
	ivec2 lod_max_coord = max(frustum.hiz_resolution >> lod, ivec2(1)) - 1;
	ix = min(ix >> lod, lod_max_coord.xx);
	iy = min(iy >> lod, lod_max_coord.yy);

	ivec2 hiz_coord = ivec2(ix.x, iy.x);

	float d = texelFetch(uHiZDepth, hiz_coord, lod).x;
	bool nx = ix.y != ix.x;
	bool ny = iy.y != iy.x;
	if (nx)
		d = max(d, texelFetchOffset(uHiZDepth, hiz_coord, lod, ivec2(1, 0)).x);
	if (ny)
		d = max(d, texelFetchOffset(uHiZDepth, hiz_coord, lod, ivec2(0, 1)).x);
	if (nx && ny)
		d = max(d, texelFetchOffset(uHiZDepth, hiz_coord, lod, ivec2(1, 1)).x);

	return closest_z < d;
}
#endif

vec2 project_sphere_flat(float view_xy, float view_z, float radius)
{
	float len = length(vec2(view_xy, view_z));
	float sin_xy = radius / len;

	float cos_xy = sqrt(1.0 - sin_xy * sin_xy);
	vec2 rot_lo = mat2(cos_xy, sin_xy, -sin_xy, cos_xy) * vec2(view_xy, view_z);
	vec2 rot_hi = mat2(cos_xy, -sin_xy, +sin_xy, cos_xy) * vec2(view_xy, view_z);

	return vec2(rot_lo.x / rot_lo.y, rot_hi.x / rot_hi.y);
}

bool cluster_cull(mat4 M, Bound bound, vec3 camera_pos)
{
	vec3 bound_center = (M * vec4(bound.center_radius.xyz, 1.0)).xyz;

	float s0 = dot(M[0].xyz, M[0].xyz);
	float s1 = dot(M[1].xyz, M[1].xyz);
	float s2 = dot(M[2].xyz, M[2].xyz);

	float max_scale_factor = sqrt(max(max(s0, s1), s2));
	float effective_radius = bound.center_radius.w * max_scale_factor;

	// Cluster cone cull.
	bool ret = true;

	vec4 cone = bound.cone;
	if (cone.w < 1.0)
	{
		cone = vec4(normalize(mat3(M) * cone.xyz), cone.w);
		ret = dot(bound_center - camera_pos, cone.xyz) <= cone.w * length(bound_center - camera_pos) + effective_radius;
	}

	for (int i = 0; i < 6 && ret; i++)
	{
		vec4 p = frustum.planes[i];
		if (dot(vec4(bound_center, 1.0), p) < -effective_radius)
			ret = false;
	}

#ifdef MESHLET_RENDER_HIZ_BINDING
	if (ret)
	{
		vec3 view = view_transform_yz_flip(bound_center);

		// Ensure there is no clipping against near plane.
		// If the sphere is close enough, we accept it.
		if (view.z > effective_radius + 0.1)
		{
			// Have to project in view space since the sphere is still a sphere.
			vec2 range_x = project_sphere_flat(view.x, view.z, effective_radius);
			vec2 range_y = project_sphere_flat(view.y, view.z, effective_radius);
			ret = hiz_cull(range_x, range_y, view.z - effective_radius);
		}
	}
#endif

	return ret;
}

#endif