#ifndef LIGHTING_H_
#define LIGHTING_H_

#include "pbr.h"

#ifdef ENVIRONMENT
layout(set = 1, binding = 0) uniform mediump samplerCube uReflection;
layout(set = 1, binding = 1) uniform mediump samplerCube uIrradiance;
#endif
layout(set = 1, binding = 2) uniform mediump sampler2D uBRDFLut;

#ifdef SHADOWS

#if !defined(DIRECTIONAL_SHADOW_PCF) && !defined(DIRECTIONAL_SHADOW_VSM)
#define DIRECTIONAL_SHADOW_PCF
#endif

#ifdef DIRECTIONAL_SHADOW_VSM
#include "vsm.h"
layout(set = 1, binding = 3) uniform mediump sampler2D uShadowmap;
#ifdef SHADOW_CASCADES
layout(set = 1, binding = 4) uniform mediump sampler2D uShadowmapNear;
#endif

mediump float get_shadow_term(
		vec4 light_clip_shadow_near,
		vec4 light_clip_shadow_far,
		vec3 light_world_pos,
		vec3 light_camera_pos,
		mediump vec3 light_camera_front,
		mediump vec3 light_direction,
		mediump float light_inv_cutoff_distance)
{
	// Sample shadowmap.
#ifdef SHADOW_CASCADES
	vec3 shadow_near = light_clip_shadow_near.xyz / light_clip_shadow_near.w;
	vec3 shadow_far = light_clip_shadow_far.xyz / light_clip_shadow_far.w;

	float view_z = dot(light_camera_front, (light_world_pos - light_camera_pos));
	float shadow_cascade = view_z * light_inv_cutoff_distance;
	mediump float shadow_term_near = 0.0;
	mediump float shadow_term_far = 0.0;

	if (shadow_cascade < 1.0)
	{
		vec2 moments_near = textureLod(uShadowmapNear, shadow_near.xy, 0.0).xy;
		shadow_term_near = vsm(shadow_near.z, moments_near);
	}

	if (shadow_cascade > 0.75)
	{
		vec2 moments_far = textureLod(uShadowmap, shadow_far.xy, 0.0).xy;
		shadow_term_far = vsm(shadow_far.z, moments_far);
	}

	mediump float shadow_lerp = clamp(4.0 * (shadow_cascade - 0.75), 0.0, 1.0);
	mediump float shadow_term = mix(shadow_term_near, shadow_term_far, shadow_lerp);
	return shadow_term;
#else
	vec3 shadow_far = light_clip_shadow_far.xyz / light_clip_shadow_far.w;
	return vsm(shadow_far.z, texture(uShadowmap, shadow_far.xy).xy);
#endif
}
#endif

#ifdef DIRECTIONAL_SHADOW_PCF
layout(set = 1, binding = 3) uniform mediump sampler2DShadow uShadowmap;
#ifdef SHADOW_CASCADES
layout(set = 1, binding = 4) uniform mediump sampler2DShadow uShadowmapNear;
#endif

#include "pcf.h"

mediump float get_shadow_term(
		vec4 light_clip_shadow_near,
		vec4 light_clip_shadow_far,
		vec3 light_world_pos,
		vec3 light_camera_pos,
		mediump vec3 light_camera_front,
		mediump vec3 light_direction,
		mediump float light_inv_cutoff_distance)
{
#ifdef SHADOW_CASCADES
	mediump float shadow_term_near = 0.0;
	mediump float shadow_term_far = 0.0;
	float view_z = dot(light_camera_front, (light_world_pos - light_camera_pos));
	float shadow_cascade = view_z * light_inv_cutoff_distance;
	if (shadow_cascade < 1.0)
	{
		SAMPLE_PCF_KERNEL(shadow_term_near, uShadowmapNear, light_clip_shadow_near);
	}
	if (shadow_cascade > 0.75)
	{
		SAMPLE_PCF_KERNEL(shadow_term_far, uShadowmap, light_clip_shadow_far);
	}
	mediump float shadow_lerp = clamp(4.0 * (shadow_cascade - 0.75), 0.0, 1.0);
	mediump float shadow_term = mix(shadow_term_near, shadow_term_far, shadow_lerp);
	return shadow_term;
#else
	mediump float shadow_term_far;
	SAMPLE_PCF_KERNEL(shadow_term_far, uShadowmap, light_clip_shadow_far);
	return shadow_term_far;
#endif
}
#endif
#endif

#ifdef RENDERER_FORWARD
#include "lighting_data.h"
#endif

#ifdef POSITIONAL_LIGHTS
#include "clusterer.h"
#endif

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
#ifdef SHADOWS
		, vec4 light_clip_shadow_near
		, vec4 light_clip_shadow_far
		, mediump float light_inv_cutoff_distance
#endif
#ifdef ENVIRONMENT
		, mediump float environment_intensity
		, mediump float environment_mipscale
#endif
		)
{
#ifdef SHADOWS
	mediump float shadow_term = get_shadow_term(
		light_clip_shadow_near,
		light_clip_shadow_far,
		light_world_pos, light_camera_pos,
		light_camera_front, light_direction, light_inv_cutoff_distance);
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
	mediump float NoL = clamp(dot(N, L), 0.0, 1.0);
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

#if 0 && defined(RENDERER_FORWARD)
	#if defined(ALPHA_TEST) && ALPHA_TEST
		const float minimum_lod = 0.0; // Can't take derivative because we might have discarded, so ...
	#else
		mediump float minimum_lod = textureQueryLod(uReflection, reflected).y;
	#endif

	mediump vec3 envspec = environment_intensity * textureLod(uReflection, reflected,
	                          max(material_roughness * environment_mipscale, minimum_lod)).rgb;
#else
	mediump vec3 envspec = environment_intensity * textureLod(uReflection, reflected, material_roughness * environment_mipscale).rgb;
#endif

	envspec *= iblspec;

	// IBL diffuse term.
	mediump vec3 envdiff = environment_intensity * textureLod(uIrradiance, N, 0.0).rgb;

	diffref += envdiff * material_ambient_factor * (1.0 - ibl_fresnel);
	specref += envspec * material_ambient_factor;
#else
	diffref += (1.0 - ibl_fresnel) * material_ambient_factor;
	specref += iblspec * material_ambient_factor;
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
		light_world_pos, light_camera_pos);
#endif

	return lighting;
}

#endif
