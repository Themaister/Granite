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

#include "spot.h"
#include "point.h"

layout(std140, set = 1, binding = 6) uniform ClusterTransform
{
	mat4 transform;
} cluster;

layout(set = 1, binding = 5) uniform usampler3D uCluster;

#define NUM_CLUSTER_HIERARCHIES 8
#define INV_NUM_CLUSTER_HIERARCHIES (1.0 / float(NUM_CLUSTER_HIERARCHIES))

vec3 compute_cluster_light(MaterialProperties material, vec3 world_pos, vec3 camera_pos)
{
	vec3 cluster_pos = (cluster.transform * vec4(world_pos, 1.0)).xyz;
	vec3 clipped_pos = abs(clamp(cluster_pos, vec3(-1.0, -1.0, 0.0), vec3(1.0)));
	vec2 max_reach2 = max(clipped_pos.xy, vec2(clipped_pos.z, 0.0001));
	float scale_factor = max(max_reach2.x, max_reach2.y);
	float level = clamp(floor(-log2(scale_factor)), 0.0, float(NUM_CLUSTER_HIERARCHIES) - 1.0);
	cluster_pos *= exp2(level);
	cluster_pos.xy = cluster_pos.xy * 0.5 + 0.5;
	cluster_pos.z = (level + cluster_pos.z) * INV_NUM_CLUSTER_HIERARCHIES;
	uvec2 bits = textureLod(uCluster, cluster_pos, 0.0).xy;

	vec3 result = vec3(0.0);

#if 0
	result.r = float(bitCount(bits.x));
	result.g = float(bitCount(bits.y));
#endif

#if 1
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

	return result;
}
#endif