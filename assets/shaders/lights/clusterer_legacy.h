#ifndef CLUSTERER_LEGACY_H_
#define CLUSTERER_LEGACY_H_

#include "../inc/global_bindings.h"

#define SPOT_LIGHT_SHADOW_ATLAS_SET 0
#define SPOT_LIGHT_SHADOW_ATLAS_BINDING BINDING_GLOBAL_CLUSTER_SPOT_LEGACY
#define POINT_LIGHT_SHADOW_ATLAS_SET 0
#define POINT_LIGHT_SHADOW_ATLAS_BINDING BINDING_GLOBAL_CLUSTER_POINT_LEGACY

layout(std140, set = 0, binding = BINDING_GLOBAL_CLUSTERER_PARAMETERS) uniform ClusterParameters
{
	ClustererParametersLegacy cluster;
};

#include "spot.h"
#include "point.h"

layout(set = 0, binding = BINDING_GLOBAL_CLUSTER_IMAGE_LEGACY) uniform usampler3D uCluster;
#ifdef CLUSTER_LIST
layout(std430, set = 0, binding = BINDING_GLOBAL_CLUSTER_LIST_LEGACY) readonly buffer ClusterList
{
	int elements[];
} cluster_list;
#endif

//#define CLUSTERING_DEBUG

const float NUM_CLUSTER_HIERARCHIES = 8.0;
const float MAX_CLUSTER_HIERARCHY = NUM_CLUSTER_HIERARCHIES - 1.0;
const float INV_PADDED_NUM_CLUSTER_HIERARCHIES = 1.0 / (NUM_CLUSTER_HIERARCHIES + 1.0);

vec3 to_cluster_pos(vec3 world_pos)
{
	vec3 cluster_pos = (cluster.transform * vec4(world_pos, 1.0)).xyz;
	float scale_factor = max(0.0001, cluster_pos.z);
	float level = clamp(ceil(log2(scale_factor)), -1.0, MAX_CLUSTER_HIERARCHY);

	// Fit scale to chosen level.
	// For level -1, the scale is actually the same scale as for level 0.
	cluster_pos *= exp2(min(-level, 0.0));

	// If level == -1.0 -> inv_z_bias == 1.0 - 1.0 == 0.0
	// If level == 0.0 -> inv_z_bias = -1.0
	// If level >= 1.0 -> inv_z_bias = -1.0
	float inv_z_bias = max(-level, 0.0) - 1.0;

	// Rescale [-1.0, 1.0] range for XY into texture space.
	cluster_pos.xy = cluster_pos.xy * 0.5 + 0.5;

	// Remap [0.5, 1.0] range to [0.0, 1.0].
	// For closest hierarchy to camera, [0.0, 0.5), we will end up with [-1.0, 0.0) here,
	// which is wrapped around to [0.0, 1.0) range.
	// Clamp away from 1.0 to avoid potential NN wraps to next level when cluster_pos.z == 1.0.
	cluster_pos.z = clamp(cluster_pos.z * 2.0 + inv_z_bias, 0.001, 0.999);

	// Remap to slice index.
	cluster_pos.z = (1.0 + level + cluster_pos.z) * INV_PADDED_NUM_CLUSTER_HIERARCHIES;

	return cluster_pos;
}

#ifdef CLUSTERER_NO_HELPER_INVOCATION
mediump vec3 compute_cluster_scatter_light(vec3 world_pos, vec3 camera_pos)
{
	vec3 cluster_pos = to_cluster_pos(world_pos);
	mediump vec3 result = vec3(0.0);

#ifdef CLUSTER_LIST
	uvec4 elements = textureLod(uCluster, cluster_pos, 0.0);
	uint spot_start = elements.x;
	uint spot_count = elements.y;
	uint point_start = elements.z;
	uint point_count = elements.w;

#ifdef CLUSTERING_DEBUG
	result.x = 0.1 * float(spot_count);
	result.y = 0.1 * float(point_count);
	return result;
#endif

	for (uint i = 0u; i < spot_count; i++)
		result += compute_spot_scatter_light(cluster_list.elements[spot_start + i], world_pos, camera_pos);
	for (uint i = 0u; i < point_count; i++)
		result += compute_point_scatter_light(cluster_list.elements[point_start + i], world_pos, camera_pos);
#else

	uvec2 bits = textureLod(uCluster, cluster_pos, 0.0).xy;

#ifdef CLUSTERING_DEBUG
	result.x = 0.1 * float(bitCount(bits.x));
	result.y = 0.1 * float(bitCount(bits.y));
	return result;
#endif

#ifdef CLUSTERING_WAVE_UNIFORM
	// Make cluster mask wave uniform for some load optimizations! :D
	uint bits_x = subgroupOr(bits.x);
#else
	uint bits_x = bits.x;
#endif

	while (bits_x != 0u)
	{
		int index = findLSB(bits_x);
		result += compute_spot_scatter_light(index, world_pos, camera_pos);
		bits_x ^= 1u << uint(index);
	}

#ifdef CLUSTERING_WAVE_UNIFORM
	// Make cluster mask wave uniform for some load optimizations! :D
	uint bits_y = subgroupOr(bits.y);
#else
	uint bits_y = bits.y;
#endif

	while (bits_y != 0u)
	{
		int index = findLSB(bits_y);
		result += compute_point_scatter_light(index, world_pos, camera_pos);
		bits_y ^= 1u << uint(index);
	}
#endif

	return result;
}
#else
mediump vec3 compute_cluster_light(
		mediump vec3 material_base_color,
		mediump vec3 material_normal,
		mediump float material_metallic,
		mediump float material_roughness,
		vec3 world_pos, vec3 camera_pos)
{
	vec3 cluster_pos = to_cluster_pos(world_pos);
	mediump vec3 result = vec3(0.0);

#ifdef CLUSTER_LIST
	uvec4 elements = textureLod(uCluster, cluster_pos, 0.0);
	uint spot_start = elements.x;
	uint spot_count = elements.y;
	uint point_start = elements.z;
	uint point_count = elements.w;

#ifdef CLUSTERING_DEBUG
	result.x = 0.1 * float(spot_count);
	result.y = 0.1 * float(point_count);
	return result;
#endif

	for (uint i = 0u; i < spot_count; i++)
	{
		result += compute_spot_light(cluster_list.elements[spot_start + i],
			material_base_color, material_normal,
			material_metallic, material_roughness, world_pos, camera_pos);
	}

	for (uint i = 0u; i < point_count; i++)
	{
		result += compute_point_light(cluster_list.elements[point_start + i],
			material_base_color, material_normal,
			material_metallic, material_roughness, world_pos, camera_pos);
	}
#else
#if defined(CLUSTERING_WAVE_UNIFORM)
	uvec2 bits = uvec2(0u);
	if (!is_helper_invocation())
		bits = textureLod(uCluster, cluster_pos, 0.0).xy;
#else
	uvec2 bits = textureLod(uCluster, cluster_pos, 0.0).xy;
#endif

#ifdef CLUSTERING_DEBUG
	result.x = 0.1 * float(bitCount(bits.x));
	result.y = 0.1 * float(bitCount(bits.y));
	return result;
#endif

#ifdef CLUSTERING_WAVE_UNIFORM
	// Make cluster mask wave uniform for some UBO load optimizations! :D
	uint bits_x = subgroupOr(bits.x);
#else
	uint bits_x = bits.x;
#endif

	while (bits_x != 0u)
	{
		int index = findLSB(bits_x);
		result += compute_spot_light(index, material_base_color, material_normal,
				material_metallic, material_roughness, world_pos, camera_pos);
		bits_x ^= 1u << uint(index);
	}

#ifdef CLUSTERING_WAVE_UNIFORM
	uint bits_y = subgroupOr(bits.y);
#else
	uint bits_y = bits.y;
#endif

	while (bits_y != 0u)
	{
		int index = findLSB(bits_y);
		result += compute_point_light(index, material_base_color, material_normal,
				material_metallic, material_roughness, world_pos, camera_pos);
		bits_y ^= 1u << uint(index);
	}
#endif

	return result;
}
#endif

#endif