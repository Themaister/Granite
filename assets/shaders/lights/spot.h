#ifndef SPOT_LIGHT_H_
#define SPOT_LIGHT_H_

#include "material.h"
#include "pbr.h"

struct SpotShaderInfo
{
	vec3 color;
	float spot_outer;

	vec3 falloff;
	float inv_radius;

	vec3 position;
	float spot_inner;

	vec3 direction;
	float xy_scale;
};

layout(std140, set = 2, binding = 0) uniform SpotParameters
{
    SpotShaderInfo data[256];
} spot;

vec3 compute_spot_light(int index, MaterialProperties material, vec3 world_pos, vec3 camera_pos)
{
	vec3 light_pos = spot.data[index].position;
	vec3 light_dir = spot.data[index].direction;
	float light_dist = length(world_pos - light_pos);
	float cone_angle = dot(normalize(world_pos - light_pos), -light_dir);
	float cone_falloff = smoothstep(spot.data[index].spot_outer, spot.data[index].spot_inner, cone_angle);
	float static_falloff = 1.0 - smoothstep(0.9, 1.0, light_dist * spot.data[index].inv_radius);
	vec3 f = spot.data[index].falloff;
	vec3 spot_color = spot.data[index].color * (cone_falloff * static_falloff / (f.x + light_dist * f.y + light_dist * light_dist * f.z));
	vec3 result = vec3(0.0);

#ifdef SPOT_LIGHT_EARLY_OUT
	if (all(equal(spot_color, vec3(0.0))))
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
	return spot_color * (reflected_light + diffuse_light);
}

#endif