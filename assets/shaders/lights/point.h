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

layout(std140, set = 2, binding = 0) uniform PointParameters
{
    PointShaderInfo data[256];
} point;

vec3 compute_point_light(int index, MaterialProperties material, vec3 world_pos, vec3 camera_pos)
{
	vec3 light_pos = point.data[index].position;
	vec3 light_dir = point.data[index].direction;
	float light_dist = length(world_pos - light_pos);
	float static_falloff = 1.0 - smoothstep(0.9, 1.0, light_dist * point.data[index].inv_radius);
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
	vec3 specref = NoL * shadow_term * cook_torrance_specular(NoL, NoV, NoH, specular_fresnel, roughness);
	vec3 diffref = NoL * shadow_term * (1.0 - specular_fresnel) * (1.0 / PI);

	vec3 reflected_light = specref;
	vec3 diffuse_light = diffref * material.base_color * (1.0 - material.metallic);
	return point_color * (reflected_light + diffuse_light);
}

#endif