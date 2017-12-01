#ifndef LIGHTING_H_
#define LIGHTING_H_

#include "pbr.h"
#include "material.h"

struct LightInfo
{
	vec3 pos;
	vec3 camera_pos;
	mediump vec3 camera_front;
	mediump vec3 direction;
	mediump vec3 color;

#ifdef SHADOWS
	vec4 clip_shadow_near;
	vec4 clip_shadow_far;
	mediump float inv_cutoff_distance;
#endif
};

#ifdef ENVIRONMENT
struct EnvironmentInfo
{
	mediump float intensity;
	mediump float mipscale;
};

layout(set = 1, binding = 0) uniform mediump samplerCube uReflection;
layout(set = 1, binding = 1) uniform mediump samplerCube uIrradiance;
#endif
layout(set = 0, binding = 7) uniform mediump sampler2D uBRDFLut;

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

mediump float get_shadow_term(LightInfo light)
{
    // Sample shadowmap.
#ifdef SHADOW_CASCADES
	vec3 shadow_near = light.clip_shadow_near.xyz / light.clip_shadow_near.w;
	vec3 shadow_far = light.clip_shadow_far.xyz / light.clip_shadow_far.w;

	vec2 moments_near = textureLod(uShadowmapNear, shadow_near.xy, 0.0).xy;
	vec2 moments_far = textureLod(uShadowmap, shadow_far.xy, 0.0).xy;

	float shadow_term_near = vsm(shadow_near.z, moments_near);
	float shadow_term_far = vsm(shadow_far.z, moments_far);

    float view_z = dot(light.camera_front, (light.pos - light.camera_pos));
    mediump float shadow_lerp = clamp(4.0 * (view_z * light.inv_cutoff_distance - 0.75), 0.0, 1.0);
    mediump float shadow_term = mix(shadow_term_near, shadow_term_far, shadow_lerp);
	return shadow_term;
#else
	vec3 shadow_far = light.clip_shadow_far.xyz / light.clip_shadow_far.w;
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

mediump float get_shadow_term(LightInfo light)
{
#ifdef SHADOW_CASCADES
	mediump float shadow_term_near;
	mediump float shadow_term_far;
	SAMPLE_PCF_KERNEL(shadow_term_near, uShadowmapNear, light.clip_shadow_near);
	SAMPLE_PCF_KERNEL(shadow_term_far, uShadowmap, light.clip_shadow_far);
    float view_z = dot(light.camera_front, (light.pos - light.camera_pos));
    mediump float shadow_lerp = clamp(4.0 * (view_z * light.inv_cutoff_distance - 0.75), 0.0, 1.0);
    mediump float shadow_term = mix(shadow_term_near, shadow_term_far, shadow_lerp);
	return shadow_term;
#else
	mediump float shadow_term_far;
	SAMPLE_PCF_KERNEL(shadow_term_far, uShadowmap, light.clip_shadow_far);
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

vec3 compute_lighting(
		MaterialProperties material,
		LightInfo light
#ifdef ENVIRONMENT
		, EnvironmentInfo environment
#endif
		)
{
#ifdef SHADOWS
	mediump float shadow_term = get_shadow_term(light);
#else
	mediump const float shadow_term = 1.0;
#endif

	mediump float roughness = material.roughness * 0.75 + 0.25;

	// Compute directional light.
	mediump vec3 L = light.direction;
	mediump vec3 V = normalize(light.camera_pos - light.pos);
	mediump vec3 H = normalize(V + L);
	mediump vec3 N = material.normal;

	mediump float NoV = clamp(dot(N, V), 0.001, 1.0);
	mediump float NoL = clamp(dot(N, L), 0.0, 1.0);
	mediump float HoV = clamp(dot(H, V), 0.001, 1.0);
	mediump float LoV = clamp(dot(L, V), 0.001, 1.0);

	mediump vec3 F0 = compute_F0(material.base_color, material.metallic);
	mediump vec3 specular_fresnel = fresnel(F0, HoV);
	mediump vec3 specref = light.color * NoL * shadow_term * cook_torrance_specular(N, H, NoL, NoV, specular_fresnel, roughness);
	mediump vec3 diffref = light.color * NoL * shadow_term * (1.0 - specular_fresnel) * (1.0 / PI);

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

	mediump vec3 envspec = environment.intensity * textureLod(uReflection, reflected,
	                          max(material.roughness * environment.mipscale, minimum_lod)).rgb;
#else
	mediump vec3 envspec = environment.intensity * textureLod(uReflection, reflected, material.roughness * environment.mipscale).rgb;
#endif

	envspec *= iblspec;

	// IBL diffuse term.
	mediump vec3 envdiff = environment.intensity * textureLod(uIrradiance, N, 0.0).rgb;

	diffref += envdiff * material.ambient_factor * (1.0 - ibl_fresnel);
	specref += envspec * material.ambient_factor;
#else
	diffref += (1.0 - ibl_fresnel) * material.ambient_factor;
	specref += iblspec * material.ambient_factor;
#endif

	mediump vec3 reflected_light = specref;
	mediump vec3 diffuse_light = diffref * material.base_color * (1.0 - material.metallic);
	mediump vec3 lighting = reflected_light + diffuse_light;

#ifdef POSITIONAL_LIGHTS
	lighting += compute_cluster_light(material, light.pos, light.camera_pos);
#endif

	return lighting;
}

#endif
