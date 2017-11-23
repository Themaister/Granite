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

#define NUM_CLUSTER_HIERARCHIES 8
#define INV_NUM_CLUSTER_HIERARCHIES (1.0 / float(NUM_CLUSTER_HIERARCHIES))

mediump vec3 compute_cluster_light(MaterialProperties material, vec3 world_pos, vec3 camera_pos)
{
	vec3 cluster_pos = (cluster.transform * vec4(world_pos, 1.0)).xyz;
	float scale_factor = max(0.0001, cluster_pos.z);
	float level = clamp(floor(-log2(scale_factor)), 0.0, float(NUM_CLUSTER_HIERARCHIES) - 1.0);
	cluster_pos *= exp2(level);
	cluster_pos.xy = cluster_pos.xy * 0.5 + 0.5;

	// Avoid potential NN wraps when cluster_pos.z == 1.0.
	cluster_pos.z = (level + clamp(cluster_pos.z, 0.001, 0.999)) * INV_NUM_CLUSTER_HIERARCHIES;
	mediump vec3 result = vec3(0.0);

#ifdef CLUSTER_LIST
	uvec4 elements = textureLod(uCluster, cluster_pos, 0.0);
	uint spot_start = elements.x;
	uint spot_count = elements.y;
	uint point_start = elements.z;
	uint point_count = elements.w;
#if 0
	result.r = float(spot_count);
	result.g = float(point_count);
#endif

#if 1
	for (uint i = 0u; i < spot_count; i++)
		result += compute_spot_light(cluster_list.elements[spot_start + i], material, world_pos, camera_pos);
	for (uint i = 0u; i < point_count; i++)
		result += compute_point_light(cluster_list.elements[point_start + i], material, world_pos, camera_pos);
#endif
#else
#if 0
	result.r = float(bitCount(bits.x));
	result.g = float(bitCount(bits.y));
#endif

#if 1
	uvec2 bits = textureLod(uCluster, cluster_pos, 0.0).xy;
	while (bits.x != 0u)
	{
		int index = findLSB(bits.x);
		result += compute_spot_light(index, material, world_pos, camera_pos);
		bits.x &= ~(1u << uint(index));
	}

	while (bits.y != 0u)
	{
		int index = findLSB(bits.y);
		result += compute_point_light(index, material, world_pos, camera_pos);
		bits.y &= ~(1u << uint(index));
	}
#endif
#endif

	return result;
}
#endif