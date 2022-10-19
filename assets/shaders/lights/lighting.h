#ifndef LIGHTING_H_
#define LIGHTING_H_

#include "lighting_resources.h"

mediump vec3 compute_lighting(
		mediump vec3 material_base_color,
		mediump vec3 material_normal,
		mediump float material_metallic,
		mediump float material_roughness,
		mediump float material_ambient_factor,
		mediump float material_transparency,
		vec3 light_world_pos,
		vec3 light_camera_pos,
		mediump vec3 light_camera_front,
		mediump vec3 light_direction,
		mediump vec3 light_color)
{
#ifdef SHADOWS
	mediump float shadow_term = get_directional_shadow_term(
		light_world_pos, light_camera_pos, light_camera_front, light_direction);
#else
	mediump const float shadow_term = 1.0;
#endif

	mediump float roughness = material_roughness * 0.75 + 0.25;

	// Compute directional light.
	mediump vec3 L = light_direction;
	mediump vec3 V = normalize(light_camera_pos - light_world_pos);
	mediump vec3 H = normalize(V + L);
	mediump vec3 N = material_normal;

	mediump float NoV = clamp(dot(N, V), 0.001, 1.0);
	mediump float NoL = clamp(dot(N, L), 0.001, 1.0);
	mediump float HoV = clamp(dot(H, V), 0.001, 1.0);
	mediump float LoV = clamp(dot(L, V), 0.001, 1.0);

	mediump vec3 F0 = compute_F0(material_base_color, material_metallic);
	mediump vec3 specular_fresnel = fresnel(F0, HoV);
	mediump vec3 specref = light_color * NoL * shadow_term * cook_torrance_specular(N, H, NoL, NoV, specular_fresnel, roughness);
	mediump vec3 diffref = light_color * NoL * shadow_term * (1.0 - specular_fresnel) * (1.0 / PI);

	mediump vec3 reflected_light = specref;
	mediump vec3 diffuse_light = diffref * material_base_color * (1.0 - material_metallic);
	mediump vec3 lighting = reflected_light + diffuse_light;

#if !defined(LIGHTING_NO_AMBIENT)
#if defined(VOLUMETRIC_DIFFUSE)
#if defined(HAS_IS_HELPER_INVOCATION)
	// Do not let helper lanes participate here since we need guarantees on which
	// lanes participate in ballots and such.
	if (!is_helper_invocation())
#endif
	{
		lighting += material_ambient_factor * compute_volumetric_diffuse_metallic(
			light_world_pos, N, material_base_color, material_metallic);
	}
#else
	lighting += material_base_color * vec3(0.05 * (1.0 - material_metallic) * material_ambient_factor);
#endif
#endif

#ifdef POSITIONAL_LIGHTS
	// Here, not letting helper lanes participate is more of an optimization
	// since we restrict the range we need to iterate over, but that's it.
#if defined(HAS_IS_HELPER_INVOCATION)
	if (!is_helper_invocation())
#endif
	{
		lighting += compute_cluster_light(
				material_base_color,
				material_normal,
				material_metallic,
				material_roughness,
				light_world_pos, light_camera_pos
				#ifdef CLUSTERER_BINDLESS
				, resolution.inv_resolution
				#endif
		);
	}
#endif

	return lighting;
}

#endif
