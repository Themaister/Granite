#version 450

#define A_GLSL 1
#define A_GPU 1
#define FSR_EASU_F 1

layout(set = 0, binding = 0) uniform sampler2D uTex;
vec4 FsrEasuRF(vec2 p) { return textureGather(uTex, p, 0); }
vec4 FsrEasuGF(vec2 p) { return textureGather(uTex, p, 1); }
vec4 FsrEasuBF(vec2 p) { return textureGather(uTex, p, 2); }

#include "ffx_a.h"
#include "ffx_fsr1.h"
#include "../../inc/srgb.h"

layout(location = 0) out vec4 FragColor;
layout(location = 0) in vec2 vUV;

layout(set = 1, binding = 0) uniform Params
{
	uvec4 param0;
	uvec4 param1;
	uvec4 param2;
	uvec4 param3;
};

void main()
{
	vec3 color;
	FsrEasuF(color, uvec2(vUV), param0, param1, param2, param3);

#if TARGET_SRGB
	color = decode_srgb(color);
#endif

	FragColor = vec4(color, 1.0);
}

