#ifndef POINT_LIGHT_H_
#define POINT_LIGHT_H_

#include "pbr.h"

struct PointShaderInfo
{
	vec3 color;
	vec3 falloff;
	float inv_radius;
	vec3 position;
	vec3 direction;
	float xy_scale;
};

#ifndef POINT_LIGHT_DATA_SET
#define POINT_LIGHT_DATA_SET 2
#endif
#ifndef POINT_LIGHT_DATA_BINDING
#define POINT_LIGHT_DATA_BINDING 0
#endif
#ifndef POINT_LIGHT_DATA_COUNT
#define POINT_LIGHT_DATA_COUNT 256
#endif

layout(std140, set = POINT_LIGHT_DATA_SET, binding = POINT_LIGHT_DATA_BINDING) uniform PointParameters
{
#ifdef POSITIONAL_LIGHT_INSTANCING
    PointShaderInfo data[POINT_LIGHT_DATA_COUNT];
#else
    PointShaderInfo data;
#endif
} point;

#ifdef POSITIONAL_LIGHTS_SHADOW
#ifndef POINT_LIGHT_SHADOW_DATA_SET
#define POINT_LIGHT_SHADOW_DATA_SET 2
#endif
#ifndef POINT_LIGHT_SHADOW_DATA_BINDING
#define POINT_LIGHT_SHADOW_DATA_BINDING 3
#endif
#ifndef POINT_LIGHT_SHADOW_DATA_COUNT
#define POINT_LIGHT_SHADOW_DATA_COUNT 256
#endif
#ifndef POINT_LIGHT_SHADOW_ATLAS_SET
#define POINT_LIGHT_SHADOW_ATLAS_SET 2
#endif
#ifndef POINT_LIGHT_SHADOW_ATLAS_BINDING
#define POINT_LIGHT_SHADOW_ATLAS_BINDING 2
#endif

struct PointShadowData
{
	vec4 transform;
	vec4 slice;
};

#ifdef POSITIONAL_SHADOW_VSM
#include "vsm.h"
layout(set = POINT_LIGHT_SHADOW_ATLAS_SET, binding = POINT_LIGHT_SHADOW_ATLAS_BINDING) uniform samplerCubeArray uPointShadowAtlas;
#else
layout(set = POINT_LIGHT_SHADOW_ATLAS_SET, binding = POINT_LIGHT_SHADOW_ATLAS_BINDING) uniform samplerCubeArrayShadow uPointShadowAtlas;
#endif

layout(std140, set = POINT_LIGHT_SHADOW_DATA_SET, binding = POINT_LIGHT_SHADOW_DATA_BINDING) uniform PointShadow
{
#ifdef POSITIONAL_LIGHT_INSTANCING
	PointShadowData data[POINT_LIGHT_SHADOW_DATA_COUNT];
#else
	PointShadowData data;
#endif
} point_shadow;
#endif

#ifdef POSITIONAL_LIGHT_INSTANCING
#define POINT_DATA(index) point.data[index]
#define POINT_SHADOW_TRANSFORM(index) point_shadow.data[index]
#else
#define POINT_DATA(index) point.data
#define POINT_SHADOW_TRANSFORM(index) point_shadow.data
#endif

vec3 compute_point_light(int index,
                         mediump vec3 material_base_color,
                         mediump vec3 material_normal,
                         mediump float material_metallic,
                         mediump float material_roughness,
                         vec3 world_pos, vec3 camera_pos)
{
	vec3 light_pos = POINT_DATA(index).position;
	vec3 light_dir_full = world_pos - light_pos;
	mediump vec3 light_dir = normalize(-light_dir_full);

#ifdef POSITIONAL_LIGHTS_SHADOW
	vec3 dir_abs = abs(light_dir_full);
	float max_z = max(max(dir_abs.x, dir_abs.y), dir_abs.z);
	vec4 shadow_transform = POINT_SHADOW_TRANSFORM(index).transform;
	mediump float slice = POINT_SHADOW_TRANSFORM(index).slice.x;
	#ifdef POSITIONAL_SHADOW_VSM
		vec2 shadow_moments = textureLod(uPointShadowAtlas, vec4(light_dir_full, slice), 0.0).xy;
		mediump float shadow_falloff = vsm(max_z, shadow_moments);
	#else
		vec2 shadow_ref2 = shadow_transform.zw - shadow_transform.xy * max_z;
		float shadow_ref = shadow_ref2.x / shadow_ref2.y;
		mediump float shadow_falloff = texture(uPointShadowAtlas, vec4(light_dir_full, slice), shadow_ref);
	#endif
#else
	const float shadow_falloff = 1.0;
#endif

	mediump float light_dist = length(world_pos - light_pos);
	mediump float static_falloff = shadow_falloff * (1.0 - smoothstep(0.9, 1.0, light_dist * POINT_DATA(index).inv_radius));
	mediump vec3 f = POINT_DATA(index).falloff;
	mediump vec3 point_color = POINT_DATA(index).color * (static_falloff / (f.x + light_dist * f.y + light_dist * light_dist * f.z));

#ifdef POINT_LIGHT_EARLY_OUT
	if (all(equal(point_color, vec3(0.0))))
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
	return point_color * (reflected_light + diffuse_light);
}

#endif
