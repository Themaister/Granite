#version 450

layout(location = 0) in vec2 aPos;
layout(location = 0) out vec3 vColor;

layout(set = 0, binding = 1) uniform Lum
{
	float lum;
};

void main()
{
	gl_Position = vec4(aPos, 0.0, 1.0);
	vColor = mix(vec3(0.0), vec3(lum), equal(ivec3(gl_VertexIndex), ivec3(0, 1, 2)));
}
