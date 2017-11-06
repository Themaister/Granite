#ifndef MATERIAL_H_
#define MATERIAL_H_

struct MaterialProperties
{
	mediump vec3 base_color;
	mediump vec3 normal;
	mediump float metallic;
	mediump float roughness;
	mediump float ambient_factor;
	mediump float transparency;
};

#endif