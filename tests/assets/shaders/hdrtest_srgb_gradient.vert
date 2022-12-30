#version 450

layout(location = 0) in vec2 aPos;
layout(location = 0) out vec3 vColor;

void main()
{
	gl_Position = vec4(aPos, 0.0, 1.0);
    vColor = mix(vec3(1.0), vec3(0.0), bvec3((gl_VertexIndex & 1) == 1));
}
