#ifndef LIGHTING_RESOURCES_H_
#define LIGHTING_RESOURCES_H_

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

mediump float get_directional_shadow_term(
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

mediump float get_directional_shadow_term(
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

#endif