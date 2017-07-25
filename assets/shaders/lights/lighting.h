#ifndef LIGHTING_H_
#define LIGHTING_H_

#include "pbr.h"

struct MaterialProperties
{
	vec3 base_color;
	vec3 normal;
	float metallic;
	float roughness;
	float ambient_factor;
	float transparency;
};

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

#ifdef SHADOWS
layout(set = 1, binding = 2) uniform sampler2D uShadowmap;
#ifdef SHADOW_CASCADES
layout(set = 1, binding = 3) uniform sampler2D uShadowmapNear;
#endif

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
	vec3 specref = NoL * shadow_term * blinn_specular(NoH, specular_fresnel, material.roughness);
	vec3 diffref = NoL * shadow_term * (1.0 - specular_fresnel) * (1.0 / PI);

#ifdef ENVIRONMENT
	// IBL diffuse term.
	//vec3 envdiff = registers.environment_intensity * textureLod(uIrradiance, N, 10.0).rgb * (1.0 / PI);
	vec3 envdiff = material.ambient_factor * mix(vec3(0.2, 0.2, 0.2) / PI, vec3(0.2, 0.2, 0.3) / PI, clamp(N.y, 0.0, 1.0));

	// IBL specular term.
	vec3 reflected = reflect(-V, N);
	//float minimum_lod = textureQueryLod(uReflection, reflected).y;
	float minimum_lod = 4.0;
	vec3 envspec = environment.intensity *
	               textureLod(uReflection, reflected,
	                          max(material.roughness * environment.mipscale, minimum_lod)).rgb;

	envspec *= 0.01;

	// Lookup reflectance terms.
	//vec2 brdf = textureLod(uBRDF, vec2(mr.y, 1.0 - NoV), 0.0).xy;
	vec2 brdf = image_based_brdf(material.roughness, NoV);

	vec3 iblspec = min(vec3(1.0), fresnel(F0, NoV) * brdf.x + brdf.y);
	envspec *= iblspec * material.ambient_factor;

	diffref += envdiff;
	specref += envspec;
#endif

	vec3 reflected_light = specref;
	vec3 diffuse_light = diffref * material.base_color * (1.0 - material.metallic);
	return light.color * (reflected_light + diffuse_light);
}

#endif
