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
} registers;

layout(location = 0) in vec2 vUV;
layout(location = 0) out mediump vec3 Color;
layout(location = 1) out mediump float OutVariance;
#include "reprojection.h"

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
    mediump float variance = textureLod(AccumVariance, reproj_uv, 0.0).x;

    history_color = convert_input(history_color);
    #if REPROJECTION_CLAMP_HISTORY
        mediump vec3 clamped_history_color = clamp_history_box(history_color, CurrentFrame, vUV, current);
        mediump float clamped_luma = luminance(clamped_history_color);
        mediump float history_luma = luminance(history_color);
        mediump float variance_delta = abs(clamped_luma - history_luma) / max(max(clamped_luma, history_luma), 0.002);
        variance_delta = clamp(variance_delta - 0.15, -0.15, 0.3);

        history_color = mix(clamped_history_color, history_color, variance);
        variance += variance_delta;
    #endif

    mediump vec3 out_color = 0.5 * (history_color + current);
    Color = convert_to_output(out_color);
    OutVariance = variance;
#else
    Color = textureLod(CurrentFrame, vUV, 0.0).rgb;
    OutVariance = 0.0;
#endif
}
