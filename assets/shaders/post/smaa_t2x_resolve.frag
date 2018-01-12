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
layout(set = 0, binding = 4) uniform mediump sampler2D AccumVariance;
#endif

layout(std430, push_constant) uniform Registers
{
    mat4 reproj;
    vec2 rt_metrics;
    float seed;
} registers;

layout(location = 0) in vec2 vUV;
layout(location = 0) out mediump vec3 Color;
layout(location = 1) out mediump vec3 OutVariance;
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
        vec2 reproj_uv = vUV - MV;
    #else
        float depth = sample_nearest_depth_box(CurrentDepth, vUV, registers.rt_metrics);
        vec4 clip = vec4(2.0 * vUV - 1.0, depth, 1.0);
        vec4 reproj_pos = registers.reproj * clip;
        vec2 reproj_uv = reproj_pos.xy / reproj_pos.w;
    #endif
    mediump vec3 history_color = textureLod(PreviousFrame, reproj_uv, 0.0).rgb;

    history_color = convert_input(history_color);
    #if REPROJECTION_CLAMP_HISTORY
        mediump vec3 clamped_history_color = clamp_history_box(history_color, CurrentFrame, vUV, current);

        vec3 in_variance =
            0.5 * textureLod(AccumVariance, reproj_uv, 0.0).rgb +
            0.125 * textureLod(AccumVariance, reproj_uv + 0.75 * registers.rt_metrics, 0.0).rgb +
            0.125 * textureLod(AccumVariance, reproj_uv - 0.75 * registers.rt_metrics, 0.0).rgb +
            0.125 * textureLod(AccumVariance, reproj_uv + vec2(0.75, -0.75) * registers.rt_metrics, 0.0).rgb +
            0.125 * textureLod(AccumVariance, reproj_uv + vec2(-0.75, +0.75) * registers.rt_metrics, 0.0).rgb;

        mediump vec3 out_error = clamped_history_color - history_color;
        out_error *= out_error;

        vec3 stddev = sqrt(in_variance);
        vec3 noise = PDnrand3(vUV + 0.01 * registers.seed + 0.69591) * 2.0 * stddev;
        in_variance = mix(in_variance, out_error, 0.1);
        OutVariance = in_variance;

        history_color = mix(clamped_history_color, history_color, clamp(10.0 * stddev, 0.0, 1.0));
    #endif

    mediump vec3 out_color = 0.5 * (history_color + current);
    Color = convert_to_output(out_color);

    //Color = clamp(10.0 * stddev, 0.0, 1.0);
#else
    Color = textureLod(CurrentFrame, vUV, 0.0).rgb;
    OutVariance = vec3(0.0);
#endif
}
