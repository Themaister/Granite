#ifndef CLUSTERER_BINDLESS_H_
#define CLUSTERER_BINDLESS_H_

#define SPOT_LIGHT_SHADOW_ATLAS_SET 4
#define POINT_LIGHT_SHADOW_ATLAS_SET 4

layout(std140, set = 0, binding = 2) uniform ClusterParameters
{
	ClustererParametersBindless cluster;
};

layout(std430, set = 0, binding = 3) readonly buffer ClustererData
{
	ClustererBindlessTransform cluster_transforms;
};

layout(std430, set = 0, binding = 4) readonly buffer ClustererBitmasks
{
	uint cluster_bitmask[];
};

layout(std430, set = 0, binding = 5) readonly buffer ClustererRanges
{
	ivec2 cluster_range[];
};

#include "spot.h"
#include "point.h"
//#define CLUSTERING_DEBUG

mediump vec3 compute_cluster_light(
		mediump vec3 material_base_color,
		mediump vec3 material_normal,
		mediump float material_metallic,
		mediump float material_roughness,
		vec3 world_pos, vec3 camera_pos)
{
	mediump vec3 result = vec3(0.0);

	ivec2 cluster_coord = ivec2(gl_FragCoord.xy * cluster.xy_scale);
	cluster_coord = clamp(cluster_index, ivec2(0), cluster.resolution_xy - 1);
	int cluster_index = cluster_coord.y * cluster.resolution_x + cluster_coord.x;
	int cluster_base = cluster_index * cluster.num_lights_32;

	float z = dot(world_pos - cluster.camera_base, cluster.camera_front);
	int z_index = int(z * cluster.z_scale);
	z_index = clamp(z_index, 0, cluster.z_max_index);

	for (int i = 0; i < cluster.num_lights_32; i++)
	{
		uint mask = cluster_bitmask[cluster_base + i];
		int type_mask = int(cluster_transforms.type_mask[i]);
		while (mask != 0u)
		{
			int bit_index = findLSB(mask);
			int index = 32 * i + bit_index;
			if ((type_mask & (1 << bit_index)) != 0)
			{
				result += compute_point_light(index, material_base_color, material_normal,
						material_metallic, material_roughness, world_pos, camera_pos);
			}
			else
			{
				result += compute_spot_light(index, material_base_color, material_normal,
						material_metallic, material_roughness, world_pos, camera_pos);
			}
			mask &= ~uint(1 << bit_index);
		}
	}

#if 0
	{
		int index = findLSB(bits_x);
		result += compute_spot_light(index, material_base_color, material_normal,
				material_metallic, material_roughness, world_pos, camera_pos);
		bits_x ^= 1u << uint(index);
	}

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