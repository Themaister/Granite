#version 450
precision highp float;
precision highp int;

#define REPROJECTION_YCgCo 1
#define REPROJECTION_HDR 0
#define REPROJECTION_CLAMP_HISTORY 1
#define REPROJECTION_MOTION_VECTORS 0

layout(set = 0, binding = 0) uniform mediump sampler2D CurrentFrame;
#if REPROJECTION_HISTORY
layout(set = 0, binding = 1) uniform sampler2D CurrentDepth;
layout(set = 0, binding = 2) uniform mediump sampler2D PreviousFrame;
#if REPROJECTION_MOTION_VECTORS
layout(set = 0, binding = 3) uniform sampler2D MVs;
#endif
#if 0
layout(set = 0, binding = 4) uniform mediump sampler2D TemporalNoise;
#endif
#endif

layout(std430, push_constant) uniform Registers
{
    mat4 reproj;
    vec2 rt_metrics;
    float seed;
} registers;

layout(location = 0) in vec2 vUV;
layout(location = 0) out mediump vec3 Color;
#include "reprojection.h"

vec3 PDnrand3(vec2 n)
{
	return 2.0 * fract(sin(dot(n.xy, vec2(12.9898, 78.233))) * vec3(43758.5453, 28001.8384, 50849.4141)) - 1.0;
}

void main()
{
#if REPROJECTION_HISTORY
    mediump vec3 current = SAMPLE_CURRENT(CurrentFrame, vUV, 0, 0);

    #if REPROJECTION_MOTION_VECTORS
        vec2 MV = sample_nearest_velocity(CurrentDepth, MVs, vUV, registers.rt_metrics);
        mediump vec3 history_color = textureLod(PreviousFrame, vUV - MV, 0.0).rgb;
    #else
        float depth = sample_nearest_depth_box(CurrentDepth, vUV, registers.rt_metrics);
        vec4 clip = vec4(2.0 * vUV - 1.0, depth, 1.0);
        vec4 reproj_pos = registers.reproj * clip;
        mediump vec3 history_color = textureProjLod(PreviousFrame, reproj_pos.xyw, 0.0).rgb;
    #endif

    history_color = convert_input(history_color);
    #if REPROJECTION_CLAMP_HISTORY
        mediump vec3 clamped_history_color = clamp_history_box(history_color, CurrentFrame, vUV, current);
        history_color = clamped_history_color + PDnrand3(vUV + 0.01 * registers.seed + 0.69591) * vec3(0.02);
    #endif

    mediump vec3 out_color = 0.5 * (history_color + current);
    Color = convert_to_output(out_color);
#else
    Color = textureLod(CurrentFrame, vUV, 0.0).rgb;
#endif
}
