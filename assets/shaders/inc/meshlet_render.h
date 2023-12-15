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

struct AABB
{
	vec3 lo; float pad0; vec3 hi; float pad;
};

struct Bound
{
	vec4 center_radius;
	vec4 cone;
};

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

layout(set = MESHLET_RENDER_DESCRIPTOR_SET, binding = MESHLET_RENDER_FRUSTUM_BINDING, std140) uniform Frustum
{
	vec4 planes[6];
} frustum;

struct TaskInfo
{
	uint aabb_instance;
	uint node_instance;
	uint node_count_material_index; // Skinning
	uint mesh_index_count;
};

layout(set = MESHLET_RENDER_DESCRIPTOR_SET, binding = MESHLET_RENDER_TASKS_BINDING, std430) readonly buffer Tasks
{
	TaskInfo data[];
} task_info;

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
	return ret;
}

#endif