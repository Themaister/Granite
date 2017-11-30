#ifndef SPOT_LIGHT_H_
#define SPOT_LIGHT_H_

#include "material.h"
#include "pbr.h"

struct SpotShaderInfo
{
	mediump vec3 color;
	mediump float spot_outer;

	mediump vec3 falloff;
	mediump float inv_radius;

	vec3 position;
	mediump float spot_inner;

	mediump vec3 direction;
	mediump float xy_scale;
};

#ifndef SPOT_LIGHT_DATA_SET
#define SPOT_LIGHT_DATA_SET 2
#endif
#ifndef SPOT_LIGHT_DATA_BINDING
#define SPOT_LIGHT_DATA_BINDING 0
#endif
#ifndef SPOT_LIGHT_DATA_COUNT
#define SPOT_LIGHT_DATA_COUNT 256
#endif

layout(std140, set = SPOT_LIGHT_DATA_SET, binding = SPOT_LIGHT_DATA_BINDING) uniform SpotParameters
{
#ifdef POSITIONAL_LIGHT_INSTANCING
    SpotShaderInfo data[SPOT_LIGHT_DATA_COUNT];
#else
    SpotShaderInfo data;
#endif
} spot;

#ifdef POSITIONAL_LIGHTS_SHADOW
#ifndef SPOT_LIGHT_SHADOW_DATA_SET
#define SPOT_LIGHT_SHADOW_DATA_SET 2
#endif
#ifndef SPOT_LIGHT_SHADOW_DATA_BINDING
#define SPOT_LIGHT_SHADOW_DATA_BINDING 3
#endif
#ifndef SPOT_LIGHT_SHADOW_DATA_COUNT
#define SPOT_LIGHT_SHADOW_DATA_COUNT 256
#endif
#ifndef SPOT_LIGHT_SHADOW_ATLAS_SET
#define SPOT_LIGHT_SHADOW_ATLAS_SET 2
#endif
#ifndef SPOT_LIGHT_SHADOW_ATLAS_BINDING
#define SPOT_LIGHT_SHADOW_ATLAS_BINDING 2
#endif

#ifdef POSITIONAL_SHADOW_VSM
#include "vsm.h"
layout(set = SPOT_LIGHT_SHADOW_ATLAS_SET, binding = SPOT_LIGHT_SHADOW_ATLAS_BINDING) uniform sampler2D uSpotShadowAtlas;
#else
layout(set = SPOT_LIGHT_SHADOW_ATLAS_SET, binding = SPOT_LIGHT_SHADOW_ATLAS_BINDING) uniform sampler2DShadow uSpotShadowAtlas;
#endif

layout(std140, set = SPOT_LIGHT_SHADOW_DATA_SET, binding = SPOT_LIGHT_SHADOW_DATA_BINDING) uniform SpotShadow
{
#ifdef POSITIONAL_LIGHT_INSTANCING
	mat4 transform[SPOT_LIGHT_SHADOW_DATA_COUNT];
#else
	mat4 transform;
#endif
} spot_shadow;
#endif

#ifdef POSITIONAL_LIGHT_INSTANCING
#define SPOT_DATA(index) spot.data[index]
#define SPOT_SHADOW_TRANSFORM(index) spot_shadow.transform[index]
#else
#define SPOT_DATA(index) spot.data
#define SPOT_SHADOW_TRANSFORM(index) spot_shadow.transform
#endif

vec3 compute_spot_light(int index, MaterialProperties material, vec3 world_pos, vec3 camera_pos)
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
		mediump float shadow_falloff = textureProjLod(uSpotShadowAtlas, spot_shadow_clip, 0.0);
	#endif
#else
	const float shadow_falloff = 1.0;
#endif
	mediump vec3 light_dir = normalize(light_pos - world_pos);
	mediump float light_dist = length(world_pos - light_pos);
	mediump float cone_angle = dot(normalize(world_pos - light_pos), light_primary_direction);
	mediump float cone_falloff = smoothstep(SPOT_DATA(index).spot_outer, SPOT_DATA(index).spot_inner, cone_angle);
	mediump float static_falloff = shadow_falloff * (1.0 - smoothstep(0.9, 1.0, light_dist * SPOT_DATA(index).inv_radius));
	mediump vec3 f = SPOT_DATA(index).falloff;
	mediump vec3 spot_color = SPOT_DATA(index).color * (cone_falloff * static_falloff / (f.x + light_dist * f.y + light_dist * light_dist * f.z));

#ifdef SPOT_LIGHT_EARLY_OUT
	if (all(equal(spot_color, vec3(0.0))))
		discard;
#endif

	mediump float roughness = material.roughness * 0.75 + 0.25;

	// Compute directional light.
	mediump vec3 L = light_dir;
	mediump vec3 V = normalize(camera_pos - world_pos);
	mediump vec3 H = normalize(V + L);
	mediump vec3 N = material.normal;

	mediump float NoV = clamp(dot(N, V), 0.001, 1.0);
	mediump float NoL = clamp(dot(N, L), 0.0, 1.0);
	mediump float HoV = clamp(dot(H, V), 0.001, 1.0);
	mediump float LoV = clamp(dot(L, V), 0.001, 1.0);

	mediump vec3 F0 = compute_F0(material.base_color, material.metallic);
	mediump vec3 specular_fresnel = fresnel(F0, HoV);
	mediump vec3 specref = NoL * cook_torrance_specular(N, H, NoL, NoV, specular_fresnel, roughness);
	mediump vec3 diffref = NoL * (1.0 - specular_fresnel) * (1.0 / PI);

	mediump vec3 reflected_light = specref;
	mediump vec3 diffuse_light = diffref * material.base_color * (1.0 - material.metallic);
	return spot_color * (reflected_light + diffuse_light);
}

#endif