#ifndef POINT_LIGHT_H_
#define POINT_LIGHT_H_

#include "pbr.h"
#include "clusterer_data.h"

#ifdef POSITIONAL_LIGHT_DEFERRED
layout(std140, set = 2, binding = 0) uniform PointParameters
{
    PointShaderInfo data[256];
} point;
#endif

#ifdef POSITIONAL_LIGHTS_SHADOW
#ifdef POSITIONAL_LIGHT_DEFERRED
#define POINT_LIGHT_SHADOW_ATLAS_SET 2
#define POINT_LIGHT_SHADOW_ATLAS_BINDING 2

layout(std140, set = 2, binding = 3) uniform PointShadowParameters
{
	PointShadowData data[256];
} point_shadow;
#endif

#ifdef POSITIONAL_SHADOW_VSM
#include "vsm.h"
layout(set = POINT_LIGHT_SHADOW_ATLAS_SET, binding = POINT_LIGHT_SHADOW_ATLAS_BINDING) uniform samplerCubeArray uPointShadowAtlas;
#else
layout(set = POINT_LIGHT_SHADOW_ATLAS_SET, binding = POINT_LIGHT_SHADOW_ATLAS_BINDING) uniform samplerCubeArrayShadow uPointShadowAtlas;
#endif
#endif

#ifdef POSITIONAL_LIGHT_DEFERRED
	#ifdef POSITIONAL_LIGHT_INSTANCING
		#define POINT_DATA(index) point.data[index]
		#define POINT_SHADOW_TRANSFORM(index) point_shadow.data[index]
	#else
		#define POINT_DATA(index) point.data[0]
		#define POINT_SHADOW_TRANSFORM(index) point_shadow.data[0]
	#endif
#else
	#define POINT_DATA(index) cluster.points[index]
	#define POINT_SHADOW_TRANSFORM(index) cluster.point_shadow[index]
#endif

mediump float point_scatter_phase_function(mediump float VoL)
{
	// Very crude :)
	return 0.5 - 0.5 * VoL;
}

const float MIN_POINT_DIST = 0.1;

mediump vec3 compute_point_color(int index, vec3 world_pos, out mediump vec3 light_dir)
{
	vec3 light_pos = POINT_DATA(index).position;
	vec3 light_dir_full = world_pos - light_pos;
	light_dir = normalize(-light_dir_full);

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

	mediump float light_dist = max(MIN_POINT_DIST, length(light_dir_full));
	mediump float static_falloff = shadow_falloff * (1.0 - smoothstep(0.9, 1.0, light_dist * POINT_DATA(index).inv_radius));
	mediump vec3 point_color = POINT_DATA(index).color * (static_falloff / (light_dist * light_dist));

	return point_color;
}

mediump vec3 compute_point_scatter_light(int index, vec3 world_pos, vec3 camera_pos)
{
	mediump vec3 light_dir;
	mediump vec3 point_color = compute_point_color(index, world_pos, light_dir);
	float VoL = dot(normalize(camera_pos - world_pos), normalize(POINT_DATA(index).position - world_pos));
	return point_color * point_scatter_phase_function(VoL);
}

mediump vec3 compute_point_light(int index,
                         mediump vec3 material_base_color,
                         mediump vec3 material_normal,
                         mediump float material_metallic,
                         mediump float material_roughness,
                         vec3 world_pos, vec3 camera_pos)
{
	mediump vec3 light_dir;
	mediump vec3 point_color = compute_point_color(index, world_pos, light_dir);

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
