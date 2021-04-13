#ifndef VOLUMETRIC_DIFFUSE_H_
#define VOLUMETRIC_DIFFUSE_H_

#include "../inc/global_bindings.h"
#include "linear_clamp_sampler.h"

layout(set = VOLUMETRIC_DIFFUSE_ATLAS_SET, binding = 0) uniform mediump texture3D uVolumes[];

struct DiffuseVolumeParameters
{
	vec4 world_to_texture[3];
	float lo_tex_coord_x;
	float hi_tex_coord_x;
	float guard_band_factor;
	float guard_band_sharpen;
};

const int CLUSTERER_MAX_VOLUMES = 128;

layout(std140, set = 0, binding = BINDING_GLOBAL_VOLUMETRIC_DIFFUSE_PARAMETERS) uniform VolumeParameters
{
	int bindless_index_offset;
	int num_volumes;
	uvec2 fallback_volume_fp16;
	DiffuseVolumeParameters volumes[CLUSTERER_MAX_VOLUMES];
} volumetric;

mediump float maximum3(mediump vec3 v)
{
	return max(max(v.x, v.y), v.z);
}

mediump float weight_term(vec3 local_pos, float factor, float sharpen)
{
	mediump float w = 0.5 - factor * maximum3(abs(local_pos - 0.5));
	return clamp(w * sharpen, 0.0, 1.0);
}

mediump vec4 compute_volumetric_diffuse(int index, vec3 world_pos, mediump vec3 normal)
{
	vec3 local_pos = vec3(
			dot(vec4(world_pos, 1.0), volumetric.volumes[index].world_to_texture[0]),
			dot(vec4(world_pos, 1.0), volumetric.volumes[index].world_to_texture[1]),
			dot(vec4(world_pos, 1.0), volumetric.volumes[index].world_to_texture[2]));

	float factor = volumetric.volumes[index].guard_band_factor;
	float sharpen = volumetric.volumes[index].guard_band_sharpen;
	mediump float w = weight_term(local_pos, factor, sharpen);

	mediump vec4 weighted_result;
	if (w > 0.0)
	{
		float base_tex_x = clamp(local_pos.x,
				volumetric.volumes[index].lo_tex_coord_x,
				volumetric.volumes[index].hi_tex_coord_x) / 6.0;

		mediump vec3 normal2 = normal * normal;
		vec3 normal_offsets = mix(vec3(0.0), vec3(1.0 / 6.0), lessThan(normal, vec3(0.0)));
		float x_offset = base_tex_x + (0.0 / 3.0) + normal_offsets.x;
		float y_offset = base_tex_x + (1.0 / 3.0) + normal_offsets.y;
		float z_offset = base_tex_x + (2.0 / 3.0) + normal_offsets.z;

#ifdef VOLUMETRIC_DIFFUSE_PREV_TEXTURES
		int tex_index = index + volumetric.bindless_index_offset + volumetric.num_volumes;
#else
		int tex_index = index + volumetric.bindless_index_offset;
#endif

		mediump vec3 result =
				normal2.x * textureLod(sampler3D(uVolumes[tex_index], LinearClampSampler), vec3(x_offset, local_pos.yz), 0.0).rgb +
				normal2.y * textureLod(sampler3D(uVolumes[tex_index], LinearClampSampler), vec3(y_offset, local_pos.yz), 0.0).rgb +
				normal2.z * textureLod(sampler3D(uVolumes[tex_index], LinearClampSampler), vec3(z_offset, local_pos.yz), 0.0).rgb;

		weighted_result = vec4(result * w, w);
	}
	else
		weighted_result = vec4(0.0);

	return weighted_result;
}

mediump vec3 compute_volumetric_diffuse(vec3 world_pos, mediump vec3 normal)
{
	mediump vec4 diffuse_weight = vec4(
			unpackHalf2x16(volumetric.fallback_volume_fp16.x),
			unpackHalf2x16(volumetric.fallback_volume_fp16.y));

	for (int i = 0; i < volumetric.num_volumes; i++)
		diffuse_weight += compute_volumetric_diffuse(i, world_pos, normal);

	// Already accounted for lambertian 1.0 / PI when creating the probe.
	return diffuse_weight.rgb / max(diffuse_weight.a, 0.0001);
}

#endif
