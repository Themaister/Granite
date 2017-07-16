#ifndef FOG_H_
#define FOG_H_

highp float fog_factor(highp vec3 eye_vec, highp float falloff)
{
	highp float distance = dot(eye_vec, eye_vec);
	return exp2(-distance * falloff);
}

mediump vec3 apply_fog(mediump vec3 color, highp vec3 eye_vec, mediump vec3 fog_color, highp float fog_falloff)
{
	highp float lerp = fog_factor(eye_vec, fog_falloff);
	return mix(fog_color, color, lerp);
}

#endif