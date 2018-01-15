#ifndef SMAA_COMMON_H_
#define SMAA_COMMON_H_

layout(std430, push_constant) uniform Registers
{
	vec4 rt_metrics;
} registers;

#define SMAA_RT_METRICS registers.rt_metrics
#define SMAA_GLSL_4

#if SMAA_QUALITY == 0
#define SMAA_PRESET_LOW
#elif SMAA_QUALITY == 1
#define SMAA_PRESET_MEDIUM
#elif SMAA_QUALITY == 2
#define SMAA_PRESET_HIGH
#elif SMAA_QUALITY == 3
#define SMAA_PRESET_ULTRA
#endif

#include "SMAA.hlsl"

#endif
