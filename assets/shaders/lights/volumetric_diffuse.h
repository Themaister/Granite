#ifndef VOLUMETRIC_DIFFUSE_H_
#define VOLUMETRIC_DIFFUSE_H_

#include "../inc/global_bindings.h"
#include "linear_clamp_sampler.h"

layout(set = VOLUMETRIC_DIFFUSE_ATLAS_SET, binding = 0) uniform mediump texture3D uVolumes[];

struct DiffuseVolumeParameters
{
	vec3 base_position;
	float lo_tex_coord_x;
	vec3 inv_extent;
	float hi_tex_coord_x;
};

const int CLUSTERER_MAX_VOLUMES = 256;

layout(std140, set = 0, binding = BINDING_GLOBAL_VOLUMETRIC_DIFFUSE_PARAMETERS) uniform VolumeParameters
{
	int bindless_index_offset;
	int num_volumes;
	DiffuseVolumeParameters volumes[CLUSTERER_MAX_VOLUMES];
} volumetric;

mediump float maximum3(mediump vec3 v)
{
	return max(max(v.x, v.y), v.z);
}

mediump float weight_term(vec3 local_pos)
{
	mediump float w = 0.5 - maximum3(abs(local_pos - 0.5));
	return clamp(w * 20.0, 0.0, 1.0);
}

mediump vec4 compute_volumetric_diffuse(int index, vec3 world_pos, mediump vec3 normal)
{
	vec3 local_pos = (world_pos - volumetric.volumes[index].base_position) * volumetric.volumes[index].inv_extent;

	mediump vec4 weighted_result;
	if (all(greaterThan(local_pos, vec3(0.0))) && all(lessThan(local_pos, vec3(1.0))))
	{
		float base_tex_x = clamp(local_pos.x,
				volumetric.volumes[index].lo_tex_coord_x,
				volumetric.volumes[index].hi_tex_coord_x) / 6.0;

		mediump vec3 normal2 = normal * normal;
		vec3 normal_offsets = mix(vec3(0.0), vec3(1.0 / 6.0), lessThan(normal, vec3(0.0)));
		float x_offset = base_tex_x + (0.0 / 3.0) + normal_offsets.x;
		float y_offset = base_tex_x + (1.0 / 3.0) + normal_offsets.y;
		float z_offset = base_tex_x + (2.0 / 3.0) + normal_offsets.z;

		int tex_index = index + volumetric.bindless_index_offset;
		mediump vec3 result =
				normal2.x * textureLod(sampler3D(uVolumes[tex_index], LinearClampSampler), vec3(x_offset, local_pos.yz), 0.0).rgb +
				normal2.y * textureLod(sampler3D(uVolumes[tex_index], LinearClampSampler), vec3(y_offset, local_pos.yz), 0.0).rgb +
				normal2.z * textureLod(sampler3D(uVolumes[tex_index], LinearClampSampler), vec3(z_offset, local_pos.yz), 0.0).rgb;

		mediump float w = weight_term(local_pos);
		weighted_result = vec4(result * w, w);
	}
	else
		weighted_result = vec4(0.0);

	return weighted_result;
}

mediump vec3 compute_volumetric_diffuse(vec3 world_pos, mediump vec3 normal)
{
	mediump vec4 diffuse_weight = vec4(0.0);
	for (int i = 0; i < volumetric.num_volumes; i++)
		diffuse_weight += compute_volumetric_diffuse(i, world_pos, normal);
	return diffuse_weight.rgb / max(diffuse_weight.a, 0.0001);
}

#endif