#ifndef FOG_H_
#define FOG_H_

layout(set = 0, binding = 1, std140) uniform FogParameters
{
	vec3 color;
	float falloff_factor;
} fog;

mediump vec3 apply_fog(mediump vec3 color, highp vec3 eye_vec)
{
	highp float distance = dot(eye_vec, eye_vec);
	highp float lerp = exp2(-distance * fog.falloff_factor);
	return mix(fog.color, color, lerp);
}

#endif