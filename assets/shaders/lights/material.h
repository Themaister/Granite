#ifndef MATERIAL_H_
#define MATERIAL_H_

struct MaterialProperties
{
	vec3 base_color;
	vec3 normal;
	float metallic;
	float roughness;
	float ambient_factor;
	float transparency;
};

#endif