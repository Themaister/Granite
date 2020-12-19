#ifndef CLUSTERER_BINDLESS_H_
#define CLUSTERER_BINDLESS_H_

#include "../inc/global_bindings.h"
#define SPOT_LIGHT_SHADOW_ATLAS_SET 1
#define POINT_LIGHT_SHADOW_ATLAS_SET 1

layout(std140, set = 0, binding = BINDING_GLOBAL_CLUSTERER_PARAMETERS) uniform ClusterParameters
{
	ClustererParametersBindless cluster;
};

layout(std430, set = 0, binding = BINDING_GLOBAL_CLUSTER_TRANSFORM) readonly buffer ClustererData
{
	ClustererBindlessTransforms cluster_transforms;
};

layout(std430, set = 0, binding = BINDING_GLOBAL_CLUSTER_BITMASK) readonly buffer ClustererBitmasks
{
	uint cluster_bitmask[];
};

layout(std430, set = 0, binding = BINDING_GLOBAL_CLUSTER_RANGE) readonly buffer ClustererRanges
{
	uvec2 cluster_range[];
};

#include "spot.h"
#include "point.h"
//#define CLUSTERING_DEBUG

uint cluster_mask_range(uint mask, uvec2 range, uint start_index)
{
	range.x = clamp(range.x, start_index, start_index + 32u);
	range.y = clamp(range.y + 1u, range.x, start_index + 32u);

	uint num_bits = range.y - range.x;
	uint range_mask = num_bits == 32 ?
		0xffffffffu :
		((1u << num_bits) - 1u) << (range.x - start_index);
	return mask & uint(range_mask);
}

#ifndef CLUSTERER_NO_HELPER_INVOCATION
mediump vec3 compute_cluster_light(
		mediump vec3 material_base_color,
		mediump vec3 material_normal,
		mediump float material_metallic,
		mediump float material_roughness,
		vec3 world_pos, vec3 camera_pos,
		vec2 inv_resolution)
{
	mediump vec3 result = vec3(0.0);

	ivec2 cluster_coord = ivec2(gl_FragCoord.xy * inv_resolution * cluster.xy_scale);
	cluster_coord = clamp(cluster_coord, ivec2(0), cluster.resolution_xy - 1);
	int cluster_index = cluster_coord.y * cluster.resolution_xy.x + cluster_coord.x;
	int cluster_base = cluster_index * cluster.num_lights_32;

	float z = dot(world_pos - cluster.camera_base, cluster.camera_front);
	int z_index = int(z * cluster.z_scale);
	z_index = clamp(z_index, 0, cluster.z_max_index);
	uvec2 z_range = cluster_range[z_index];

#ifdef CLUSTERING_WAVE_UNIFORM
	int z_start = int(subgroupMin(z_range.x) >> 5u);
	int z_end = int(subgroupMax(z_range.y) >> 5u);
#else
	int z_start = int(z_range.x >> 5u);
	int z_end = int(z_range.y >> 5u);
#endif

	for (int i = z_start; i <= z_end; i++)
	{
		uint mask = cluster_bitmask[cluster_base + i];
		mask = cluster_mask_range(mask, z_range, 32u * i);
#ifdef CLUSTERING_WAVE_UNIFORM
		mask = subgroupOr(mask);
#endif

		int type_mask = int(cluster_transforms.type_mask[i]);
		while (mask != 0u)
		{
			int bit_index = findLSB(mask);
			int index = 32 * i + bit_index;
			if ((type_mask & (1 << bit_index)) != 0)
			{
				result += compute_point_light(index, material_base_color, material_normal,
						material_metallic, material_roughness, world_pos, camera_pos);
#ifdef CLUSTERING_DEBUG
				result += vec3(0.1, 0.0, 0.0);
#endif
			}
			else
			{
				result += compute_spot_light(index, material_base_color, material_normal,
						material_metallic, material_roughness, world_pos, camera_pos);
#ifdef CLUSTERING_DEBUG
				result += vec3(0.0, 0.1, 0.0);
#endif
			}
			mask &= ~uint(1 << bit_index);
		}
	}

	return result;
}
#else
mediump vec3 compute_cluster_scatter_light(vec3 world_pos, vec3 camera_pos)
{
	mediump vec3 result = vec3(0.0);

	vec4 clip_coord = cluster.transform * vec4(world_pos, 1.0);
	if (clip_coord.w <= 0.0)
		return result;
	ivec2 cluster_coord = ivec2((clip_coord.xy * cluster.xy_scale) / clip_coord.w);

	cluster_coord = clamp(cluster_coord, ivec2(0), cluster.resolution_xy - 1);
	int cluster_index = cluster_coord.y * cluster.resolution_xy.x + cluster_coord.x;
	int cluster_base = cluster_index * cluster.num_lights_32;

	float z = dot(world_pos - cluster.camera_base, cluster.camera_front);
	int z_index = int(z * cluster.z_scale);
	z_index = clamp(z_index, 0, cluster.z_max_index);
	uvec2 z_range = cluster_range[z_index];

#ifdef CLUSTERING_WAVE_UNIFORM
	int z_start = int(subgroupMin(z_range.x) >> 5u);
	int z_end = int(subgroupMax(z_range.y) >> 5u);
#else
	int z_start = int(z_range.x >> 5u);
	int z_end = int(z_range.y >> 5u);
#endif

	for (int i = z_start; i <= z_end; i++)
	{
		uint mask = cluster_bitmask[cluster_base + i];
		mask = cluster_mask_range(mask, z_range, 32u * i);
#ifdef CLUSTERING_WAVE_UNIFORM
		mask = subgroupOr(mask);
#endif

		int type_mask = int(cluster_transforms.type_mask[i]);
		while (mask != 0u)
		{
			int bit_index = findLSB(mask);
			int index = 32 * i + bit_index;
			if ((type_mask & (1 << bit_index)) != 0)
			{
				result += compute_point_scatter_light(index, world_pos, camera_pos);
			}
			else
			{
				result += compute_spot_scatter_light(index, world_pos, camera_pos);
			}
			mask &= ~uint(1 << bit_index);
		}
	}

	return result;
}
#endif

#endif