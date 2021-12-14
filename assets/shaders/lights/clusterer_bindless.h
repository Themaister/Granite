#ifndef CLUSTERER_BINDLESS_H_
#define CLUSTERER_BINDLESS_H_

#include "clusterer_bindless_buffers.h"

#define SPOT_LIGHT_SHADOW_ATLAS_SET 1
#define POINT_LIGHT_SHADOW_ATLAS_SET 1
#define VOLUMETRIC_DIFFUSE_ATLAS_SET 1

#ifdef VOLUMETRIC_DIFFUSE
#include "volumetric_diffuse.h"
#endif

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

#if defined(SUBGROUP_FRAGMENT)
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

#ifdef SUBGROUP_OPS
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
#ifdef SUBGROUP_OPS
		mask = subgroupOr(mask);
#endif

		int type_mask = int(cluster_transforms.type_mask[i]);
		while (mask != 0u)
		{
			int bit_index = findLSB(mask);
			int index = 32 * i + bit_index;
			if ((type_mask & (1 << bit_index)) != 0)
			{
				result += compute_point_light(index, POINT_DATA(index), material_base_color, material_normal,
						material_metallic, material_roughness, world_pos, camera_pos);
#ifdef CLUSTERING_DEBUG
				result += vec3(0.1, 0.0, 0.0);
#endif
			}
			else
			{
				result += compute_spot_light(index, SPOT_DATA(index), material_base_color, material_normal,
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

#elif defined(SUBGROUP_COMPUTE)

#ifdef CLUSTERER_GLOBAL
mediump vec3 compute_cluster_irradiance_light(vec3 world_pos, mediump vec3 normal)
{
	mediump vec3 result = vec3(0.0);
	int count = cluster_global_transforms.num_lights;
	uint type_mask = cluster_global_transforms.type_mask;

#if defined(SUBGROUP_OPS) && (defined(SUBGROUP_COMPUTE_FULL) || defined(SUBGROUP_SHUFFLE))
	vec3 aabb_lo = subgroupMin(world_pos);
	vec3 aabb_hi = subgroupMax(world_pos);
	vec3 aabb_radius3 = 0.5 * (aabb_hi - aabb_lo);
	float aabb_radius = subgroupBroadcastFirst(length(aabb_radius3));
	vec3 aabb_center = subgroupBroadcastFirst(0.5 * (aabb_lo + aabb_hi));

#if defined(SUBGROUP_COMPUTE_FULL)
	int active_lanes = int(gl_SubgroupSize);
	int bit_offset = int(gl_SubgroupInvocationID);
#else
	uvec4 ballot = subgroupBallot(true);
	int active_lanes = int(subgroupBallotBitCount(ballot));
	int bit_offset = int(subgroupBallotExclusiveBitCount(ballot));
#endif

	// Wave uniform loop
	for (int i = 0; i < count; i += active_lanes)
	{
		int current_index = i + bit_offset;
		PositionalLightInfo light_info;

		bool active_volume = false;
		if (current_index < count)
		{
			// SPOT_DATA == POINT_DATA for bindless.
			light_info = SPOT_DATA(current_index);
			vec2 offset_radius = unpackHalf2x16(light_info.offset_radius);
			float radius = offset_radius.y;
			float shortest_distance = length(light_info.position + light_info.direction * offset_radius.x - aabb_center);
			// Treats spot lights as points, garbage culling, but probably good enough in practice.
			active_volume = shortest_distance < (radius + aabb_radius);
		}

		uvec4 active_ballot = subgroupBallot(active_volume);
		// Wave uniform loop
		while (any(notEqual(active_ballot, uvec4(0u))))
		{
			int bit_index = int(subgroupBallotFindLSB(active_ballot));
			active_ballot &= subgroupBallot(bit_index != gl_SubgroupInvocationID);

#if defined(SUBGROUP_COMPUTE_FULL)
			int index = i + bit_index;
#else
			int index = subgroupShuffle(current_index, bit_index);
#endif

#if defined(SUBGROUP_SHUFFLE)
			PositionalLightInfo scalar_light;
			scalar_light.color = subgroupShuffle(light_info.color, bit_index);
			scalar_light.spot_scale_bias = subgroupShuffle(light_info.spot_scale_bias, bit_index);
			scalar_light.position = subgroupShuffle(light_info.position, bit_index);
			scalar_light.direction = subgroupShuffle(light_info.direction, bit_index);
			scalar_light.inv_radius = subgroupShuffle(light_info.inv_radius, bit_index);
#elif defined(SUBGROUP_COMPUTE_FULL)
			PositionalLightInfo scalar_light = SPOT_DATA(index);
#endif

			if ((type_mask & (1u << index)) != 0u)
				result += compute_irradiance_point_light(index, scalar_light, normal, world_pos);
			else
				result += compute_irradiance_spot_light(index, scalar_light, normal, world_pos);
		}
	}
#else
	for (int i = 0; i < count; i++)
	{
		if ((type_mask & (1u << i)) != 0u)
			result += compute_irradiance_point_light(i, POINT_DATA(i), normal, world_pos);
		else
			result += compute_irradiance_spot_light(i, SPOT_DATA(i), normal, world_pos);
	}
#endif
	return result;
}
#endif

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

#ifdef SUBGROUP_OPS
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
#ifdef SUBGROUP_OPS
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
