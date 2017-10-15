#ifndef POINT_LIGHT_H_
#define POINT_LIGHT_H_

#include "material.h"
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
    PointShaderInfo data[POINT_LIGHT_DATA_COUNT];
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

layout(set = POINT_LIGHT_SHADOW_ATLAS_SET, binding = POINT_LIGHT_SHADOW_ATLAS_BINDING) uniform samplerCubeArrayShadow uPointShadowAtlas;
layout(std140, set = POINT_LIGHT_SHADOW_DATA_SET, binding = POINT_LIGHT_SHADOW_DATA_BINDING) uniform PointShadow
{
	vec4 transform[POINT_LIGHT_SHADOW_DATA_COUNT];
} point_shadow;

#ifdef POINT_LIGHT_TRANSLATE_SLICE
layout(set = 2, binding = 4) uniform PointShadowSlice
{
	vec4 slice[POINT_LIGHT_SHADOW_DATA_COUNT];
} point_slice;
#endif
#endif

vec3 compute_point_light(int index, MaterialProperties material, vec3 world_pos, vec3 camera_pos)
{
	vec3 light_pos = point.data[index].position;
	vec3 light_dir_full = world_pos - light_pos;
	vec3 light_dir = normalize(-light_dir_full);

#ifdef POSITIONAL_LIGHTS_SHADOW
	vec3 dir_abs = abs(light_dir_full);
	float max_z = max(max(dir_abs.x, dir_abs.y), dir_abs.z);
	vec4 shadow_transform = point_shadow.transform[index];
	vec2 shadow_ref2 = shadow_transform.zw - shadow_transform.xy * max_z;
	float shadow_ref = shadow_ref2.x / shadow_ref2.y;
	#ifdef POINT_LIGHT_TRANSLATE_SLICE
		float slice = point_slice.slice[index].x;
	#else
		float slice = float(index);
	#endif
	float shadow_falloff = texture(uPointShadowAtlas, vec4(light_dir_full, slice), shadow_ref);
#else
	const float shadow_falloff = 1.0;
#endif

	float light_dist = length(world_pos - light_pos);
	float static_falloff = shadow_falloff * (1.0 - smoothstep(0.9, 1.0, light_dist * point.data[index].inv_radius));
	vec3 f = point.data[index].falloff;
	vec3 point_color = point.data[index].color * (static_falloff / (f.x + light_dist * f.y + light_dist * light_dist * f.z));

#ifdef POINT_LIGHT_EARLY_OUT
	if (all(equal(point_color, vec3(0.0))))
		discard;
#endif

	float roughness = material.roughness * 0.75 + 0.25;

	// Compute directional light.
	vec3 L = light_dir;
	vec3 V = normalize(camera_pos - light_pos);
	vec3 H = normalize(V + L);
	vec3 N = material.normal;

	float NoH = clamp(dot(N, H), 0.0, 1.0);
	float NoV = clamp(dot(N, V), 0.001, 1.0);
	float NoL = clamp(dot(N, L), 0.0, 1.0);
	float HoV = clamp(dot(H, V), 0.001, 1.0);
	float LoV = clamp(dot(L, V), 0.001, 1.0);

	vec3 F0 = compute_F0(material.base_color, material.metallic);
	vec3 specular_fresnel = fresnel(F0, HoV);
	vec3 specref = NoL * cook_torrance_specular(NoL, NoV, NoH, specular_fresnel, roughness);
	vec3 diffref = NoL * (1.0 - specular_fresnel) * (1.0 / PI);

	vec3 reflected_light = specref;
	vec3 diffuse_light = diffref * material.base_color * (1.0 - material.metallic);
	return point_color * (reflected_light + diffuse_light);
}

#endif