#ifndef SPOT_LIGHT_H_
#define SPOT_LIGHT_H_

#include "pbr.h"
#include "clusterer_data.h"

#ifdef POSITIONAL_LIGHT_DEFERRED
layout(std140, set = 2, binding = 0) uniform SpotParameters
{
    SpotShaderInfo data[256];
} spot;
#else
#include "lighting_data.h"
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
layout(set = SPOT_LIGHT_SHADOW_ATLAS_SET, binding = SPOT_LIGHT_SHADOW_ATLAS_BINDING) uniform sampler2D uSpotShadowAtlas;
#else
#include "pcf.h"
layout(set = SPOT_LIGHT_SHADOW_ATLAS_SET, binding = SPOT_LIGHT_SHADOW_ATLAS_BINDING) uniform sampler2DShadow uSpotShadowAtlas;
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
#else
	#define SPOT_DATA(index) clusterer.spots[index]
	#define SPOT_SHADOW_TRANSFORM(index) clusterer.spot_shadow[index]
#endif

mediump vec3 compute_spot_light(int index,
                                mediump vec3 material_base_color,
                                mediump vec3 material_normal,
                                mediump float material_metallic,
                                mediump float material_roughness,
                                vec3 world_pos, vec3 camera_pos)
{
	vec3 light_pos = SPOT_DATA(index).position;
	vec3 light_primary_direction = SPOT_DATA(index).direction;
#ifdef POSITIONAL_LIGHTS_SHADOW
	#ifdef POSITIONAL_SHADOW_VSM
		vec4 spot_shadow_clip = SPOT_SHADOW_TRANSFORM(index) * vec4(world_pos, 1.0);
		vec2 shadow_uv = spot_shadow_clip.xy / spot_shadow_clip.w;
		vec2 shadow_moments = textureLod(uSpotShadowAtlas, shadow_uv, 0.0).xy;
		float shadow_z = dot(light_primary_direction, world_pos - light_pos);
		mediump float shadow_falloff = vsm(shadow_z, shadow_moments);
	#else
		vec4 spot_shadow_clip = SPOT_SHADOW_TRANSFORM(index) * vec4(world_pos, 1.0);
		mediump float shadow_falloff;
		SAMPLE_PCF_KERNEL(shadow_falloff, uSpotShadowAtlas, spot_shadow_clip);
	#endif
#else
	const float shadow_falloff = 1.0;
#endif
	mediump vec3 light_dir_full = light_pos - world_pos;
	mediump vec3 light_dir = normalize(light_dir_full);
	mediump float light_dist = length(light_dir_full);
	mediump float cone_angle = dot(normalize(world_pos - light_pos), light_primary_direction);
	mediump float cone_falloff = smoothstep(SPOT_DATA(index).spot_outer, SPOT_DATA(index).spot_inner, cone_angle);
	mediump float static_falloff = shadow_falloff * (1.0 - smoothstep(0.9, 1.0, light_dist * SPOT_DATA(index).inv_radius));
	mediump vec3 spot_color = SPOT_DATA(index).color * ((static_falloff * cone_falloff) / (light_dist * light_dist));

#ifdef SPOT_LIGHT_EARLY_OUT
	if (all(equal(spot_color, vec3(0.0))))
		discard;
#endif

	mediump float roughness = material_roughness * 0.75 + 0.25;

	// Compute directional light.
	mediump vec3 L = light_dir;
	mediump vec3 V = normalize(camera_pos - world_pos);
	mediump vec3 H = normalize(V + L);
	mediump vec3 N = material_normal;

	mediump float NoV = clamp(dot(N, V), 0.001, 1.0);
	mediump float NoL = clamp(dot(N, L), 0.0, 1.0);
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
