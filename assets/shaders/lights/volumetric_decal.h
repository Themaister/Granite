#ifndef VOLUMETRIC_DECAL_H_
#define VOLUMETRIC_DECAL_H_

#ifndef CLUSTERER_BINDLESS
#define CLUSTERER_BINDLESS
#endif

#extension GL_EXT_nonuniform_qualifier : require
#include "clusterer_bindless_buffers.h"
#include "../inc/subgroup_extensions.h"

layout(std430, set = 0, binding = BINDING_GLOBAL_CLUSTER_BITMASK_DECAL) readonly buffer ClustererBitmasksDecal
{
	uint cluster_bitmask_decal[];
};

layout(std430, set = 0, binding = BINDING_GLOBAL_CLUSTER_RANGE_DECAL) readonly buffer ClustererRangesDecal
{
	uvec2 cluster_range_decal[];
};

layout(set = 1, binding = 0) uniform texture2D TexDecals[];
#include "linear_geometry_sampler.h"
#include "lighting_data.h"

void apply_volumetric_decals(inout mediump vec4 base_color,
                             vec3 world_pos)
{
	vec2 inv_resolution = resolution.inv_resolution;
	ivec2 cluster_coord = ivec2(gl_FragCoord.xy * inv_resolution * cluster.xy_scale);
	cluster_coord = clamp(cluster_coord, ivec2(0), cluster.resolution_xy - 1);
	int cluster_index = cluster_coord.y * cluster.resolution_xy.x + cluster_coord.x;
	int cluster_base = cluster_index * cluster.num_decals_32;

	float z = dot(world_pos - cluster.camera_base, cluster.camera_front);
	int z_index = int(z * cluster.z_scale);
	z_index = clamp(z_index, 0, cluster.z_max_index);
	uvec2 z_range = cluster_range_decal[z_index];

#ifdef SUBGROUP_OPS
	int z_start = int(subgroupMin(z_range.x) >> 5u);
	int z_end = int(subgroupMax(z_range.y) >> 5u);
#else
	int z_start = int(z_range.x >> 5u);
	int z_end = int(z_range.y >> 5u);
#endif

	for (int i = z_start; i <= z_end; i++)
	{
		uint mask = cluster_bitmask_decal[cluster_base + i];
		mask = cluster_mask_range(mask, z_range, 32u * i);
#ifdef SUBGROUP_OPS
		mask = subgroupOr(mask);
#endif

		while (mask != 0u)
		{
			int bit_index = findLSB(mask);
			int index = 32 * i + bit_index;

			vec4 row0 = cluster_transforms.decals[index].world_to_texture[0];
			vec4 row1 = cluster_transforms.decals[index].world_to_texture[1];
			vec4 row2 = cluster_transforms.decals[index].world_to_texture[2];
			float tex_u = dot(row0, vec4(world_pos, 1.0));
			float tex_v = dot(row1, vec4(world_pos, 1.0));
			float tex_w = dot(row2, vec4(world_pos, 1.0));
			vec3 uvw = vec3(tex_u, tex_v, tex_w);

			bool in_range = all(lessThan(abs(uvw), vec3(0.5)));
			mediump vec4 decal_color;
			// Ideally, quad any.
			if (subgroupAny(in_range))
				decal_color = texture(nonuniformEXT(sampler2D(TexDecals[cluster.decals_texture_offset + index], LinearGeometrySampler)), uvw.xy + 0.5);

			if (in_range)
				base_color = mix(base_color, decal_color, decal_color.a);

#ifdef CLUSTERING_DEBUG
			base_color.r += 0.2;
#endif
			mask &= ~uint(1 << bit_index);
		}
	}
}

#endif