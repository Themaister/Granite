#ifndef CLUSTERER_H_
#define CLUSTERER_H_

#define SPOT_LIGHT_DATA_SET 1
#define SPOT_LIGHT_DATA_BINDING 7
#define SPOT_LIGHT_DATA_COUNT 32

#define SPOT_LIGHT_SHADOW_ATLAS_SET 1
#define SPOT_LIGHT_SHADOW_ATLAS_BINDING 9

#define SPOT_LIGHT_SHADOW_DATA_SET 1
#define SPOT_LIGHT_SHADOW_DATA_BINDING 10
#define SPOT_LIGHT_SHADOW_DATA_COUNT 32

#define POINT_LIGHT_DATA_SET 1
#define POINT_LIGHT_DATA_BINDING 8
#define POINT_LIGHT_DATA_COUNT 32

#define POINT_LIGHT_SHADOW_ATLAS_SET 1
#define POINT_LIGHT_SHADOW_ATLAS_BINDING 11

#define POINT_LIGHT_SHADOW_DATA_SET 1
#define POINT_LIGHT_SHADOW_DATA_BINDING 12
#define POINT_LIGHT_SHADOW_DATA_COUNT 32

#define POSITIONAL_LIGHT_INSTANCING

#include "spot.h"
#include "point.h"

layout(std140, set = 1, binding = 6) uniform ClusterTransform
{
	mat4 transform;
} cluster;

layout(set = 1, binding = 5) uniform usampler3D uCluster;
#ifdef CLUSTER_LIST
layout(std430, set = 1, binding = 13) readonly buffer ClusterList
{
	int elements[];
} cluster_list;
#endif

//#define CLUSTERING_DEBUG

const float NUM_CLUSTER_HIERARCHIES = 8.0;
const float MAX_CLUSTER_HIERARCHY = NUM_CLUSTER_HIERARCHIES - 1.0;
const float INV_PADDED_NUM_CLUSTER_HIERARCHIES = 1.0 / (NUM_CLUSTER_HIERARCHIES + 1.0);

mediump vec3 compute_cluster_light(
		mediump vec3 material_base_color,
		mediump vec3 material_normal,
		mediump float material_metallic,
		mediump float material_roughness,
		vec3 world_pos, vec3 camera_pos)
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
		result += compute_spot_light(cluster_list.elements[spot_start + i], material, world_pos, camera_pos);
	for (uint i = 0u; i < point_count; i++)
		result += compute_point_light(cluster_list.elements[point_start + i], material, world_pos, camera_pos);
#else
	uvec2 bits = textureLod(uCluster, cluster_pos, 0.0).xy;

#ifdef CLUSTERING_DEBUG
	result.x = 0.1 * float(bitCount(bits.x));
	result.y = 0.1 * float(bitCount(bits.y));
	return result;
#endif

	while (bits.x != 0u)
	{
		int index = findLSB(bits.x);
		result += compute_spot_light(index, material_base_color, material_normal,
		                             material_metallic, material_roughness, world_pos, camera_pos);
		bits.x &= ~(1u << uint(index));
	}

	while (bits.y != 0u)
	{
		int index = findLSB(bits.y);
		result += compute_point_light(index, material_base_color, material_normal,
		                              material_metallic, material_roughness, world_pos, camera_pos);
		bits.y &= ~(1u << uint(index));
	}
#endif

	return result;
}
#endif
