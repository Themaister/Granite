#ifndef CUBE_COORDINATES_H_
#define CUBE_COORDINATES_H_

const vec3 base_dirs[6] = vec3[](
	vec3(1.0, 0.0, 0.0),
	vec3(-1.0, 0.0, 0.0),
	vec3(0.0, 1.0, 0.0),
	vec3(0.0, -1.0, 0.0),
	vec3(0.0, 0.0, 1.0),
	vec3(0.0, 0.0, -1.0));

const vec3 pos_du[6] = vec3[](
	vec3(0.0, 0.0, -1.0),
	vec3(0.0, 0.0, +1.0),
	vec3(1.0, 0.0, 0.0),
	vec3(1.0, 0.0, 0.0),
	vec3(1.0, 0.0, 0.0),
	vec3(-1.0, 0.0, 0.0));

const vec3 pos_dv[6] = vec3[](
	vec3(0.0, -1.0, 0.0),
	vec3(0.0, -1.0, 0.0),
	vec3(0.0, 0.0, +1.0),
	vec3(0.0, 0.0, -1.0),
	vec3(0.0, -1.0, 0.0),
	vec3(0.0, -1.0, 0.0));

#endif