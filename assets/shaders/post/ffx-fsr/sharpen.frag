#version 450

#define A_GLSL 1
#define A_GPU 1
#define FSR_RCAS_F 1

layout(set = 0, binding = 0) uniform sampler2D uTex;
vec4 FsrRcasLoadF(ivec2 p) { return texelFetch(uTex, p, 0); }
void FsrRcasInputF(inout float r, inout float g, inout float b) {}

#include "ffx_a.h"
#include "ffx_fsr1.h"

layout(location = 0) out vec4 FragColor;
layout(location = 0) in vec2 vUV;

layout(set = 1, binding = 0) uniform UBO
{
	uvec4 param0;
};

void main()
{
	vec3 color;
	FsrRcasF(color.r, color.g, color.b, uvec2(vUV), param0);
	FragColor = vec4(color, 1.0);
}

