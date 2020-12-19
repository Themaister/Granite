#ifndef LIGHTING_RESOURCES_H_
#define LIGHTING_RESOURCES_H_

#include "pbr.h"
#include "../inc/global_bindings.h"

#ifdef ENVIRONMENT
layout(set = 0, binding = BINDING_GLOBAL_ENV_RADIANCE) uniform mediump samplerCube uReflection;
layout(set = 0, binding = BINDING_GLOBAL_ENV_IRRADIANCE) uniform mediump samplerCube uIrradiance;
#endif

layout(set = 0, binding = BINDING_GLOBAL_BRDF_TABLE) uniform mediump sampler2D uBRDFLut;

#ifdef RENDERER_FORWARD
#include "lighting_data.h"
#endif

#ifdef SHADOWS

#ifndef SHADOW_TRANSFORMS
#error "Must define SHADOW_TRANSFORMS."
#endif

#ifndef SHADOW_CASCADE_LOG_BIAS
#error "Must define SHADOW_CASCADE_LOG_BIAS."
#endif

#ifndef SHADOW_NUM_CASCADES
#error "Must define SHADOW_NUM_CASCADES."
#endif

#if !defined(DIRECTIONAL_SHADOW_PCF) && !defined(DIRECTIONAL_SHADOW_VSM)
#define DIRECTIONAL_SHADOW_PCF
#endif

#ifdef SHADOW_CASCADES
void compute_shadow_cascade(out vec3 clip_near, out vec3 clip_far,
		out mediump float shadow_lerp, out mediump float white_lerp,
		out mediump int layer_near, out mediump int layer_far,
		vec3 light_world_pos, vec3 light_camera_pos,
		mediump vec3 light_camera_front, mediump vec3 light_direction)
{
	float view_z = max(dot(light_camera_front, (light_world_pos - light_camera_pos)), 0.0);
	float shadow_cascade = log2(view_z) + SHADOW_CASCADE_LOG_BIAS;

	shadow_cascade = max(shadow_cascade, 0.0);
	layer_near = min(int(shadow_cascade), SHADOW_NUM_CASCADES - 1);
	layer_far = min(layer_near + 1, SHADOW_NUM_CASCADES - 1);

	const float BEGIN_LERP_FRACT = 0.8;
	const float INV_BEGIN_LERP_FRACT = 1.0 / (1.0 - BEGIN_LERP_FRACT);

	shadow_lerp = INV_BEGIN_LERP_FRACT * max(fract(shadow_cascade) - BEGIN_LERP_FRACT, 0.0);
	if (layer_near == layer_far)
		shadow_lerp = 0.0;
	else if (shadow_lerp == 0.0)
		layer_far = layer_near;

#if defined(SUBGROUP_ARITHMETIC)
	mediump int wave_minimum_layer = subgroupMin(layer_near);
	mediump int wave_maximum_layer = subgroupMax(layer_far);
	for (mediump int i = wave_minimum_layer; i <= wave_maximum_layer; i++)
	{
		// Ensures that we get a scalar load of the shadow transform matrix.
		vec3 new_clip = (SHADOW_TRANSFORMS[i] * vec4(light_world_pos, 1.0)).xyz;
		if (i == layer_near)
			clip_near = new_clip;
		else if (i == layer_far)
			clip_far = new_clip;
	}
#else
	clip_near = (SHADOW_TRANSFORMS[layer_near] * vec4(light_world_pos, 1.0)).xyz;
	if (shadow_lerp > 0.0)
		clip_far = (SHADOW_TRANSFORMS[layer_far] * vec4(light_world_pos, 1.0)).xyz;
#endif

	// Out of range, blend to full illumination.
	const float MAX_CASCADE = float(SHADOW_NUM_CASCADES);
	const float INV_MAX_CASCADE = 100.0 / MAX_CASCADE;
	white_lerp = clamp(INV_MAX_CASCADE * (shadow_cascade - 0.99 * MAX_CASCADE), 0.0, 1.0);
}
#endif

#ifdef DIRECTIONAL_SHADOW_VSM
#include "vsm.h"
#ifdef SHADOW_CASCADES
layout(set = 0, binding = BINDING_GLOBAL_DIRECTIONAL_SHADOW) uniform sampler2DArray uShadowmap;
#else
layout(set = 0, binding = BINDING_GLOBAL_DIRECTIONAL_SHADOW) uniform sampler2D uShadowmap;
#endif

mediump float get_directional_shadow_term(
		vec3 light_world_pos,
		vec3 light_camera_pos,
		mediump vec3 light_camera_front,
		mediump vec3 light_direction)
{
	// Sample shadowmap.
#ifdef SHADOW_CASCADES
	vec3 clip_near, clip_far;
	mediump float shadow_lerp, white_lerp, shadow_term, shadow_term_far;
	mediump int layer_near, layer_far;
	compute_shadow_cascade(clip_near, clip_far, shadow_lerp, white_lerp, layer_near, layer_far,
			light_world_pos, light_camera_pos,
			light_camera_front, light_direction);
	shadow_term = vsm(clip_near.z, textureLod(uShadowmap, vec3(clip_near.xy, layer_near), 0.0).xy);
	if (shadow_lerp > 0.0)
	{
		shadow_term_far = vsm(clip_far.z, textureLod(uShadowmap, vec3(clip_far.xy, layer_far), 0.0).xy);
		shadow_term = mix(shadow_term, shadow_term_far, shadow_lerp);
	}
	shadow_term = mix(shadow_term, 1.0, white_lerp);
	return shadow_term;
#else
	vec3 shadow_far = (SHADOW_TRANSFORMS[0] * vec4(light_world_pos, 1.0)).xyz;
	return vsm(shadow_far.z, textureLod(uShadowmap, shadow_far.xy, 0.0).xy);
#endif
}
#endif

#ifdef DIRECTIONAL_SHADOW_PCF
#ifdef SHADOW_CASCADES
layout(set = 0, binding = BINDING_GLOBAL_DIRECTIONAL_SHADOW) uniform mediump sampler2DArrayShadow uShadowmap;
#else
layout(set = 0, binding = BINDING_GLOBAL_DIRECTIONAL_SHADOW) uniform mediump sampler2DShadow uShadowmap;
#endif

#include "pcf.h"

mediump float get_directional_shadow_term(
		vec3 light_world_pos,
		vec3 light_camera_pos,
		mediump vec3 light_camera_front,
		mediump vec3 light_direction)
{
#ifdef SHADOW_CASCADES
	vec3 clip_near, clip_far;
	mediump float shadow_lerp, white_lerp, shadow_term, shadow_term_far;
	mediump int layer_near, layer_far;
	compute_shadow_cascade(clip_near, clip_far, shadow_lerp, white_lerp, layer_near, layer_far,
			light_world_pos, light_camera_pos,
			light_camera_front, light_direction);
	SAMPLE_PCF_KERNEL_LAYER_NOPROJ(shadow_term, uShadowmap, clip_near, layer_near);
	if (shadow_lerp > 0.0)
	{
		SAMPLE_PCF_KERNEL_LAYER_NOPROJ(shadow_term_far, uShadowmap, clip_far, layer_far);
		shadow_term = mix(shadow_term, shadow_term_far, shadow_lerp);
	}
	shadow_term = mix(shadow_term, 1.0, white_lerp);
	return shadow_term;
#else
	mediump float shadow_term_far;
	vec3 light_clip_shadow_far = (SHADOW_TRANSFORMS[0] * vec4(light_world_pos, 1.0)).xyz;
	SAMPLE_PCF_KERNEL(shadow_term_far, uShadowmap, vec4(light_clip_shadow_far, 1.0));
	return shadow_term_far;
#endif
}
#endif
#endif

#ifdef POSITIONAL_LIGHTS
#include "clusterer.h"
#endif

#ifdef AMBIENT_OCCLUSION
layout(set = 0, binding = BINDING_GLOBAL_AMBIENT_OCCLUSION) uniform mediump sampler2D uAmbientOcclusion;
#endif

#endif
