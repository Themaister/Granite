#ifndef SPOT_LIGHT_H_
#define SPOT_LIGHT_H_

#include "pbr.h"
#include "clusterer_data.h"

#ifdef POSITIONAL_LIGHT_DEFERRED
layout(std140, set = 2, binding = 0) uniform SpotParameters
{
    PositionalLightInfo data[256];
} spot;
#endif

#ifdef POSITIONAL_LIGHTS_SHADOW
#ifdef POSITIONAL_LIGHT_DEFERRED
#define SPOT_LIGHT_SHADOW_ATLAS_SET 2
#define SPOT_LIGHT_SHADOW_ATLAS_BINDING 2

layout(std140, set = 2, binding = 3) uniform SpotShadowParameters
{
	mat4 data[256];
} spot_shadow;
#endif

#ifdef POSITIONAL_SHADOW_VSM
#include "vsm.h"
#if defined(CLUSTERER_BINDLESS)
layout(set = SPOT_LIGHT_SHADOW_ATLAS_SET, binding = 0) uniform texture2D uSpotShadowAtlas[];
#else
layout(set = SPOT_LIGHT_SHADOW_ATLAS_SET, binding = SPOT_LIGHT_SHADOW_ATLAS_BINDING) uniform sampler2D uSpotShadowAtlas;
#endif
#else
#include "pcf.h"
#if defined(CLUSTERER_BINDLESS)
layout(set = SPOT_LIGHT_SHADOW_ATLAS_SET, binding = 0) uniform texture2D uSpotShadowAtlas[];
#else
layout(set = SPOT_LIGHT_SHADOW_ATLAS_SET, binding = SPOT_LIGHT_SHADOW_ATLAS_BINDING) uniform sampler2DShadow uSpotShadowAtlas;
#endif
#endif
#endif

#ifdef POSITIONAL_LIGHT_DEFERRED
	#ifdef POSITIONAL_LIGHT_INSTANCING
		#define SPOT_DATA(index) spot.data[index]
		#define SPOT_SHADOW_TRANSFORM(index) spot_shadow.data[index]
	#else
		#define SPOT_DATA(index) spot.data[0]
		#define SPOT_SHADOW_TRANSFORM(index) spot_shadow.data[0]
	#endif
#elif defined(CLUSTERER_BINDLESS)
	#define SPOT_DATA(index) cluster_transforms.lights[index]
	#define SPOT_SHADOW_TRANSFORM(index) cluster_transforms.shadow[index]
#else
	#define SPOT_DATA(index) cluster.spots[index]
	#define SPOT_SHADOW_TRANSFORM(index) cluster.spot_shadow[index]
#endif

mediump float spot_scatter_phase_function(mediump float VoL)
{
	// Very crude :)
	return 0.55 - 0.45 * VoL;
}

const float MIN_SPOT_DIST = 0.1;

mediump vec3 compute_spot_color(int index, vec3 world_pos, out mediump vec3 light_dir)
{
	vec3 light_pos = SPOT_DATA(index).position;
	vec3 light_primary_direction = SPOT_DATA(index).direction;

	mediump vec3 light_dir_full = light_pos - world_pos;
	light_dir = normalize(light_dir_full);
	mediump float light_dist = max(MIN_SPOT_DIST, length(light_dir_full));
	mediump float cone_angle = dot(normalize(world_pos - light_pos), light_primary_direction);
	mediump float cone_falloff = clamp(cone_angle * SPOT_DATA(index).spot_scale + SPOT_DATA(index).spot_bias, 0.0, 1.0);
	cone_falloff *= cone_falloff;
	cone_falloff *= 1.0 - smoothstep(0.9, 1.0, light_dist * SPOT_DATA(index).inv_radius);

	mediump vec3 spot_color;
	if (cone_falloff > 0.0)
	{
#ifdef POSITIONAL_LIGHTS_SHADOW
	#ifdef POSITIONAL_SHADOW_VSM
		vec4 spot_shadow_clip = SPOT_SHADOW_TRANSFORM(index) * vec4(world_pos, 1.0);
		vec2 shadow_uv = spot_shadow_clip.xy / spot_shadow_clip.w;
		#ifdef CLUSTERER_BINDLESS
			vec2 shadow_moments = textureLod(nonuniformEXT(sampler2D(uSpotShadowAtlas[index], LinearClampSampler)), shadow_uv, 0.0).xy;
		#else
			vec2 shadow_moments = textureLod(uSpotShadowAtlas, shadow_uv, 0.0).xy;
		#endif
		float shadow_z = dot(light_primary_direction, world_pos - light_pos);
		mediump float shadow_falloff = vsm(shadow_z, shadow_moments);
	#else
		vec4 spot_shadow_clip = SPOT_SHADOW_TRANSFORM(index) * vec4(world_pos, 1.0);
		mediump float shadow_falloff;
		#ifdef CLUSTERER_BINDLESS
			SAMPLE_PCF_KERNEL_BINDLESS(shadow_falloff, uSpotShadowAtlas, index, spot_shadow_clip);
		#else
			SAMPLE_PCF_KERNEL(shadow_falloff, uSpotShadowAtlas, spot_shadow_clip);
		#endif
	#endif
#else
		const float shadow_falloff = 1.0;
#endif
		spot_color = SPOT_DATA(index).color * ((cone_falloff * shadow_falloff) / (light_dist * light_dist));
	}
	else
		spot_color = vec3(0.0);

	return spot_color;
}

mediump vec3 compute_spot_scatter_light(int index, vec3 world_pos, vec3 camera_pos)
{
	mediump vec3 light_dir;
	mediump vec3 spot_color = compute_spot_color(index, world_pos, light_dir);
	float VoL = dot(normalize(camera_pos - world_pos), normalize(SPOT_DATA(index).position - world_pos));
	return spot_color * spot_scatter_phase_function(VoL);
}

mediump vec3 compute_spot_light(int index,
                                mediump vec3 material_base_color,
                                mediump vec3 material_normal,
                                mediump float material_metallic,
                                mediump float material_roughness,
                                vec3 world_pos, vec3 camera_pos)
{
	mediump vec3 light_dir;
	mediump vec3 spot_color = compute_spot_color(index, world_pos, light_dir);

#ifdef SPOT_LIGHT_EARLY_OUT
	if (all(equal(spot_color, vec3(0.0))))
		discard;
#else
	if (all(equal(spot_color, vec3(0.0))))
		return spot_color;
#endif

	mediump float roughness = material_roughness * 0.75 + 0.25;

	// Compute directional light.
	mediump vec3 L = light_dir;
	mediump vec3 V = normalize(camera_pos - world_pos);
	mediump vec3 H = normalize(V + L);
	mediump vec3 N = material_normal;

	mediump float NoV = clamp(dot(N, V), 0.001, 1.0);
	mediump float NoL = clamp(dot(N, L), 0.001, 1.0);
	mediump float HoV = clamp(dot(H, V), 0.001, 1.0);
	mediump float LoV = clamp(dot(L, V), 0.001, 1.0);

	mediump vec3 F0 = compute_F0(material_base_color, material_metallic);
	mediump vec3 specular_fresnel = fresnel(F0, HoV);
	mediump vec3 specref = NoL * cook_torrance_specular(N, H, NoL, NoV, specular_fresnel, roughness);
	mediump vec3 diffref = NoL * (1.0 - specular_fresnel) * (1.0 / PI);

	mediump vec3 reflected_light = specref;
	mediump vec3 diffuse_light = diffref * material_base_color * (1.0 - material_metallic);
	return spot_color * (reflected_light + diffuse_light);
}

#endif
