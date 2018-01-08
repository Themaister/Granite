#ifndef SMAA_COMMON_H_
#define SMAA_COMMON_H_

layout(std430, push_constant) uniform Registers
{
	vec4 rt_metrics;
} registers;

#define SMAA_RT_METRICS registers.rt_metrics
#define SMAA_GLSL_4
#define SMAA_PRESET_ULTRA
#include "SMAA.hlsl"

#endif