#ifndef CLUSTERER_H_
#define CLUSTERER_H_

#define SPOT_LIGHT_DATA_SET 1
#define SPOT_LIGHT_DATA_BINDING 7
#define SPOT_LIGHT_DATA_COUNT 32

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

vec3 compute_cluster_light(MaterialProperties material, vec3 world_pos, vec3 camera_pos)
{
	vec3 result = vec3(0.0);
	vec3 cluster_pos = (cluster.transform * vec4(world_pos, 1.0)).xyz;

	if (all(lessThan(cluster_pos, vec3(1.0))) && all(greaterThan(cluster_pos, vec3(0.0))))
	{
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
	}

	return result;
}
#endif