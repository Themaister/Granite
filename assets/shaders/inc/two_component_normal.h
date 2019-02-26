#ifndef TWO_COMPONENT_NORMAL_H_
#define TWO_COMPONENT_NORMAL_H_

mediump vec3 two_component_normal(mediump vec2 N)
{
	return vec3(N, sqrt(max(1.0 - dot(N, N), 0.0)));
}

#endif
