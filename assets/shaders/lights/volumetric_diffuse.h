#ifndef VOLUMETRIC_DIFFUSE_H_
#define VOLUMETRIC_DIFFUSE_H_

#include "../inc/global_bindings.h"
#include "linear_clamp_sampler.h"
#include "pbr.h"

layout(set = VOLUMETRIC_DIFFUSE_ATLAS_SET, binding = 0) uniform mediump texture3D uVolumes[];

struct DiffuseVolumeParameters
{
	vec4 world_to_texture[3];
	vec4 world_lo;
	vec4 world_hi;
	float lo_tex_coord_x;
	float hi_tex_coord_x;
	float guard_band_factor;
	float guard_band_sharpen;
};

const int CLUSTERER_MAX_VOLUMES = 128;

layout(std140, set = 0, binding = BINDING_GLOBAL_VOLUMETRIC_DIFFUSE_PARAMETERS) uniform VolumeParameters
{
	vec3 sun_direction;
	int bindless_index_offset;
	vec3 sun_color;
	int num_volumes;
	DiffuseVolumeParameters volumes[CLUSTERER_MAX_VOLUMES];
} volumetric;

layout(set = 0, binding = BINDING_GLOBAL_VOLUMETRIC_DIFFUSE_FALLBACK_VOLUME)
uniform mediump textureBuffer uVolumetricDiffuseFallback;

mediump float maximum3(mediump vec3 v)
{
	return max(max(v.x, v.y), v.z);
}

mediump float weight_term(vec3 local_pos, float factor, float sharpen)
{
	mediump float w = 0.5 - factor * maximum3(abs(local_pos - 0.5));
	return clamp(w * sharpen, 0.0, 1.0);
}

mediump vec4 compute_volumetric_diffuse(int index, DiffuseVolumeParameters volume, vec3 world_pos, mediump vec3 normal)
{
	vec3 local_pos = vec3(
			dot(vec4(world_pos, 1.0), volume.world_to_texture[0]),
			dot(vec4(world_pos, 1.0), volume.world_to_texture[1]),
			dot(vec4(world_pos, 1.0), volume.world_to_texture[2]));

	float factor = volume.guard_band_factor;
	float sharpen = volume.guard_band_sharpen;
	mediump float w = weight_term(local_pos, factor, sharpen);

	mediump vec4 weighted_result;
	if (w > 0.0)
	{
		float base_tex_x = clamp(local_pos.x, volume.lo_tex_coord_x, volume.hi_tex_coord_x) / 6.0;

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
				normal2.x * textureLod(nonuniformEXT(sampler3D(uVolumes[tex_index], LinearClampSampler)), vec3(x_offset, local_pos.yz), 0.0).rgb +
				normal2.y * textureLod(nonuniformEXT(sampler3D(uVolumes[tex_index], LinearClampSampler)), vec3(y_offset, local_pos.yz), 0.0).rgb +
				normal2.z * textureLod(nonuniformEXT(sampler3D(uVolumes[tex_index], LinearClampSampler)), vec3(z_offset, local_pos.yz), 0.0).rgb;

		weighted_result = vec4(result * w, w);
	}
	else
		weighted_result = vec4(0.0);

	return weighted_result;
}

mediump vec3 compute_volumetric_diffuse(vec3 world_pos, mediump vec3 normal, bool active_lane)
{
	mediump ivec3 coords = mix(ivec3(0, 2, 4), ivec3(1, 3, 5), lessThan(normal, vec3(0.0)));
	mediump vec3 normal2 = normal * normal;
	mediump vec3 result =
			normal2.x * texelFetch(uVolumetricDiffuseFallback, coords.x).rgb +
			normal2.y * texelFetch(uVolumetricDiffuseFallback, coords.y).rgb +
			normal2.z * texelFetch(uVolumetricDiffuseFallback, coords.z).rgb;
	mediump vec4 diffuse_weight = vec4(result * 0.01, 0.01);

#if defined(SUBGROUP_OPS) && (defined(SUBGROUP_COMPUTE_FULL) || defined(SUBGROUP_SHUFFLE))
	const float FLT_BIG = 1e38;
	vec3 aabb_lo = subgroupMin(active_lane ? world_pos : vec3(FLT_BIG));
	vec3 aabb_hi = subgroupMax(active_lane ? world_pos : vec3(-FLT_BIG));

#if defined(SUBGROUP_COMPUTE_FULL)
	int active_lanes = int(gl_SubgroupSize);
	int bit_offset = int(gl_SubgroupInvocationID);
#else
	uvec4 ballot = subgroupBallot(true);
	int active_lanes = int(subgroupBallotBitCount(ballot));
	int bit_offset = int(subgroupBallotExclusiveBitCount(ballot));
#endif

	// Wave uniform loop
	for (int i = 0; i < volumetric.num_volumes; i += active_lanes)
	{
		int current_index = i + bit_offset;
		DiffuseVolumeParameters volume;

		// Test by intersecting AABBs.
		bool active_volume = false;
		if (current_index < volumetric.num_volumes)
		{
			volume = volumetric.volumes[current_index];
			active_volume = all(greaterThan(aabb_hi, volume.world_lo.xyz)) && all(lessThan(aabb_lo, volume.world_hi.xyz));
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

			// Wave uniform lane index.
#if defined(SUBGROUP_SHUFFLE)
			DiffuseVolumeParameters scalar_volume;
			scalar_volume.world_to_texture[0] = subgroupShuffle(volume.world_to_texture[0], bit_index);
			scalar_volume.world_to_texture[1] = subgroupShuffle(volume.world_to_texture[1], bit_index);
			scalar_volume.world_to_texture[2] = subgroupShuffle(volume.world_to_texture[2], bit_index);
			scalar_volume.lo_tex_coord_x = subgroupShuffle(volume.lo_tex_coord_x, bit_index);
			scalar_volume.hi_tex_coord_x = subgroupShuffle(volume.hi_tex_coord_x, bit_index);
			scalar_volume.guard_band_factor = subgroupShuffle(volume.guard_band_factor, bit_index);
			scalar_volume.guard_band_sharpen = subgroupShuffle(volume.guard_band_sharpen, bit_index);
#elif defined(SUBGROUP_COMPUTE_FULL)
			DiffuseVolumeParameters scalar_volume = volumetric.volumes[i + bit_index];
#endif

			diffuse_weight += compute_volumetric_diffuse(index, scalar_volume, world_pos, normal);
		}
	}
#else
	// Naive path.
	if (active)
		for (int i = 0; i < volumetric.num_volumes; i++)
			diffuse_weight += compute_volumetric_diffuse(i, volumetric.volumes[i], world_pos, normal);
#endif

	// Already accounted for lambertian 1.0 / PI when creating the probe.
	return diffuse_weight.rgb / max(diffuse_weight.a, 0.0001);
}

mediump vec3 compute_volumetric_diffuse_metallic(
		vec3 pos, mediump vec3 N,
		mediump vec3 base_color,
		mediump float metallic)
{
	mediump vec3 irradiance = compute_volumetric_diffuse(pos, N, true);
	return base_color * irradiance * (1.0 - metallic);
}

#endif
