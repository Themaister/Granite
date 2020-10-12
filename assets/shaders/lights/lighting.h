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
		mediump vec3 light_color
#ifdef ENVIRONMENT
		, mediump float environment_intensity
		, mediump float environment_mipscale
#endif
		)
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

	// Lookup reflectance terms.
	mediump vec2 brdf = textureLod(uBRDFLut, vec2(NoV, roughness), 0.0).xy;
	mediump vec3 ibl_fresnel = fresnel_ibl(F0, NoV, roughness);
	mediump vec3 iblspec = ibl_fresnel * brdf.x + brdf.y;

#ifdef ENVIRONMENT
	// IBL specular term.
	mediump vec3 reflected = reflect(-V, N);
	mediump vec3 envspec = environment_intensity * textureLod(uReflection, reflected, material_roughness * environment_mipscale).rgb;

	envspec *= iblspec;

	// IBL diffuse term.
	mediump vec3 envdiff = environment_intensity * textureLod(uIrradiance, N, 0.0).rgb;

	diffref += envdiff * material_ambient_factor * (1.0 - ibl_fresnel);
	specref += envspec * material_ambient_factor;
#else
	diffref += 0.1 * material_ambient_factor;
#endif

	mediump vec3 reflected_light = specref;
	mediump vec3 diffuse_light = diffref * material_base_color * (1.0 - material_metallic);
	mediump vec3 lighting = reflected_light + diffuse_light;

#ifdef POSITIONAL_LIGHTS
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
#endif

	return lighting;
}

#endif
