#ifndef LIGHTING_H_
#define LIGHTING_H_

#include "pbr.h"
#include "material.h"

struct LightInfo
{
	vec3 pos;
	vec3 camera_pos;
	vec3 camera_front;
	vec3 direction;
	vec3 color;

#ifdef SHADOWS
	vec4 clip_shadow_near;
	vec4 clip_shadow_far;
	float inv_cutoff_distance;
#endif
};

#ifdef ENVIRONMENT
struct EnvironmentInfo
{
	float intensity;
	float mipscale;
};

layout(set = 1, binding = 0) uniform samplerCube uReflection;
layout(set = 1, binding = 1) uniform samplerCube uIrradiance;
#endif
layout(set = 0, binding = 7) uniform sampler2D uBRDFLut;

#ifdef SHADOWS
layout(set = 1, binding = 3) uniform highp sampler2DShadow uShadowmap;
#ifdef SHADOW_CASCADES
layout(set = 1, binding = 4) uniform highp sampler2DShadow uShadowmapNear;
#endif

#define SHADOW_PCF
#ifdef SHADOW_VSM
float vsm(float depth, vec2 moments)
{
    float shadow_term = 1.0f;
    if (depth > moments.x)
    {
        float variance = max(moments.y - moments.x * moments.x, 0.00001);
        float d = depth - moments.x;
        shadow_term = variance / (variance + d * d);
        shadow_term = clamp((shadow_term - 0.25) / 0.75, 0.0, 1.0); // Avoid some lighting leaking.
    }
    return shadow_term;
}
#endif

#ifdef SHADOW_VSM
float get_shadow_term(LightInfo light)
{
    // Sample shadowmap.
#ifdef SHADOW_CASCADES
	vec3 shadow_near = light.clip_shadow_near.xyz / light.clip_shadow_near.w;
	vec3 shadow_far = light.clip_shadow_far.xyz / light.clip_shadow_far.w;
	float shadow_term_near = vsm(shadow_near.z, texture(uShadowmapNear, shadow_near.xy).xy);
	float shadow_term_far = vsm(shadow_far.z, texture(uShadowmap, shadow_far.xy).xy);
    float view_z = dot(light.camera_front, (light.pos - light.camera_pos));
    float shadow_lerp = clamp(4.0 * (view_z * light.inv_cutoff_distance - 0.75), 0.0, 1.0);
    float shadow_term = mix(shadow_term_near, shadow_term_far, shadow_lerp);
	return shadow_term;
#else
	vec3 shadow_far = light.clip_shadow_far.xyz / light.clip_shadow_far.w;
	return vsm(shadow_far.z, texture(uShadowmap, shadow_far.xy).xy);
#endif
}
#elif defined(SHADOW_PCF)
float get_shadow_term(LightInfo light)
{
#ifdef SHADOW_CASCADES
	float shadow_term_near = textureProjLod(uShadowmapNear, light.clip_shadow_near, 0.0);
	float shadow_term_far = textureProjLod(uShadowmap, light.clip_shadow_far, 0.0);
    float view_z = dot(light.camera_front, (light.pos - light.camera_pos));
    float shadow_lerp = clamp(4.0 * (view_z * light.inv_cutoff_distance - 0.75), 0.0, 1.0);
    float shadow_term = mix(shadow_term_near, shadow_term_far, shadow_lerp);
	return shadow_term;
#else
	float shadow_term_far = textureProjLod(uShadowmap, light.clip_shadow_far, 0.0);
	return shadow_term_far;
#endif
}
#endif
#endif

#ifdef RENDERER_FORWARD
#include "lighting_data.h"
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
	float shadow_term = get_shadow_term(light);
#else
	const float shadow_term = 1.0;
#endif

	float roughness = material.roughness * 0.75 + 0.25;

	// Compute directional light.
	vec3 L = light.direction;
	vec3 V = normalize(light.camera_pos - light.pos);
	vec3 H = normalize(V + L);
	vec3 N = material.normal;

	float NoH = clamp(dot(N, H), 0.0, 1.0);
	float NoV = clamp(dot(N, V), 0.001, 1.0);
	float NoL = clamp(dot(N, L), 0.0, 1.0);
	float HoV = clamp(dot(H, V), 0.001, 1.0);
	float LoV = clamp(dot(L, V), 0.001, 1.0);

	vec3 F0 = compute_F0(material.base_color, material.metallic);
	vec3 specular_fresnel = fresnel(F0, HoV);
	vec3 specref = light.color * NoL * shadow_term * cook_torrance_specular(NoL, NoV, NoH, specular_fresnel, roughness);
	vec3 diffref = light.color * NoL * shadow_term * (1.0 - specular_fresnel) * (1.0 / PI);

	// Lookup reflectance terms.
	vec2 brdf = textureLod(uBRDFLut, vec2(NoV, roughness), 0.0).xy;
	vec3 ibl_fresnel = fresnel_ibl(F0, NoV, roughness);
	vec3 iblspec = ibl_fresnel * brdf.x + brdf.y;

#ifdef ENVIRONMENT
	// IBL specular term.
	vec3 reflected = reflect(-V, N);

#if defined(ALPHA_TEST) && ALPHA_TEST
	float minimum_lod = environment.mipscale; // Can't take derivative because we might have discarded, so ...
#else
	float minimum_lod = textureQueryLod(uReflection, reflected).y + 1.0;
#endif

	vec3 envspec = environment.intensity *
	               textureLod(uReflection, reflected,
	                          max(material.roughness * environment.mipscale, minimum_lod)).rgb;

	envspec *= iblspec;

	// IBL diffuse term.
	vec3 envdiff = environment.intensity * texture(uIrradiance, N).rgb;

	diffref += envdiff * material.ambient_factor * (1.0 - ibl_fresnel);
	specref += envspec * material.ambient_factor;
#else
	diffref += (1.0 - ibl_fresnel) * material.ambient_factor;
	specref += iblspec * material.ambient_factor;
#endif

	vec3 reflected_light = specref;
	vec3 diffuse_light = diffref * material.base_color * (1.0 - material.metallic);
	return reflected_light + diffuse_light;
}

#endif
