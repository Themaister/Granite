#ifndef LIGHTING_IRRADIANCE_H_
#define LIGHTING_IRRADIANCE_H_

#include "lighting_resources.h"

mediump vec3 compute_irradiance_lighting(
		vec3 light_world_pos,
		vec3 light_camera_pos,
		mediump vec3 light_camera_front,
		mediump vec3 light_normal,
		mediump vec3 light_direction,
		mediump vec3 light_color)
{
#ifdef SHADOWS
	mediump float shadow_term = get_directional_shadow_term(
			light_world_pos, light_camera_pos,
			light_camera_front, light_direction);
#else
	mediump const float shadow_term = 1.0;
#endif

	// Don't consider specular or PBR here. It's too directional in nature
	// to be meaningful for hemisphere integrals. Instead, assume dielectic, fully diffuse, max rough materials.
	mediump float NoL = clamp(dot(light_normal, light_direction), 0.0, 1.0);
	mediump vec3 in_light = light_color * shadow_term * NoL;

#ifdef POSITIONAL_LIGHTS
	in_light += compute_cluster_irradiance_light(light_world_pos, light_normal);
#endif

	return in_light;
}
#endif
