#version 450
precision highp float;
precision highp int;

#if TAA_QUALITY == 0
    #define REPROJECTION_YCgCo 0
    #define REPROJECTION_HDR 0
    #define REPROJECTION_CLAMP_HISTORY 1
    #define REPROJECTION_UNBIASED_LUMA 1
    #define REPROJECTION_CUBIC_HISTORY 0
    #define REPROJECTION_CLAMP_METHOD REPROJECTION_CLAMP_METHOD_CLAMP
    #define NEIGHBOR_METHOD NEIGHBOR_METHOD_5TAP_CROSS
    #define NEAREST_METHOD NEAREST_METHOD_5TAP_CROSS
#elif TAA_QUALITY == 1
    #define REPROJECTION_YCgCo 0
    #define REPROJECTION_HDR 1
    #define REPROJECTION_CLAMP_HISTORY 1
    #define REPROJECTION_UNBIASED_LUMA 1
    #define REPROJECTION_CUBIC_HISTORY 0
    #define REPROJECTION_CLAMP_METHOD REPROJECTION_CLAMP_METHOD_CLAMP
    #define NEIGHBOR_METHOD NEIGHBOR_METHOD_5TAP_CROSS
    #define NEAREST_METHOD NEAREST_METHOD_5TAP_CROSS
#elif TAA_QUALITY == 2
    #define REPROJECTION_YCgCo 0
    #define REPROJECTION_HDR 1
    #define REPROJECTION_CLAMP_HISTORY 1
    #define REPROJECTION_UNBIASED_LUMA 1
    #define REPROJECTION_CUBIC_HISTORY 0
    #define REPROJECTION_CLAMP_METHOD REPROJECTION_CLAMP_METHOD_AABB
    #define NEIGHBOR_METHOD NEIGHBOR_METHOD_ROUNDED_CORNER
    #define NEAREST_METHOD NEAREST_METHOD_5TAP_CROSS
#elif TAA_QUALITY == 3
    #define REPROJECTION_YCgCo 1
    #define REPROJECTION_HDR 1
    #define REPROJECTION_CLAMP_HISTORY 1
    #define REPROJECTION_UNBIASED_LUMA 1
    #define REPROJECTION_CUBIC_HISTORY 0
    #define REPROJECTION_CLAMP_METHOD REPROJECTION_CLAMP_METHOD_AABB
    #define NEIGHBOR_METHOD NEIGHBOR_METHOD_ROUNDED_CORNER
    #define NEAREST_METHOD NEAREST_METHOD_5TAP_CROSS
#elif TAA_QUALITY == 4
    #define REPROJECTION_YCgCo 1
    #define REPROJECTION_HDR 1
    #define REPROJECTION_CLAMP_HISTORY 1
    #define REPROJECTION_UNBIASED_LUMA 1
    #define REPROJECTION_CUBIC_HISTORY 0
    #define REPROJECTION_CLAMP_METHOD REPROJECTION_CLAMP_METHOD_AABB
    #define NEIGHBOR_METHOD NEIGHBOR_METHOD_ROUNDED_CORNER_VARIANCE
    #define NEAREST_METHOD NEAREST_METHOD_3x3
#elif TAA_QUALITY == 5
    #define REPROJECTION_YCgCo 1
    #define REPROJECTION_HDR 1
    #define REPROJECTION_CLAMP_HISTORY 1
    #define REPROJECTION_UNBIASED_LUMA 1
    #define REPROJECTION_CUBIC_HISTORY 1
    #define REPROJECTION_CLAMP_METHOD REPROJECTION_CLAMP_METHOD_AABB
    #define NEIGHBOR_METHOD NEIGHBOR_METHOD_ROUNDED_CORNER_VARIANCE
    #define NEAREST_METHOD NEAREST_METHOD_3x3
#else
#error "Unknown TAA quality."
#endif

#define REPROJECTION_MOTION_VECTORS 0

layout(set = 0, binding = 0) uniform mediump sampler2D CurrentFrame;
#if REPROJECTION_HISTORY
layout(set = 0, binding = 1) uniform sampler2D CurrentDepth;
layout(set = 0, binding = 2) uniform mediump sampler2D PreviousFrame;
#if REPROJECTION_MOTION_VECTORS
layout(set = 0, binding = 3) uniform sampler2D MVs;
#endif
#endif

layout(std430, push_constant) uniform Registers
{
    mat4 reproj;
    vec4 rt_metrics;
} registers;

layout(location = 0) in vec2 vUV;
layout(location = 0) out mediump vec3 Color;
#include "reprojection.h"

void main()
{
#if REPROJECTION_HISTORY
    mediump vec3 current = SAMPLE_CURRENT(CurrentFrame, vUV, 0, 0);

    #if REPROJECTION_MOTION_VECTORS
        vec2 MV = sample_nearest_velocity(CurrentDepth, MVs, vUV, registers.rt_metrics.xy);
        #if REPROJECTION_CUBIC_HISTORY
            mediump vec3 history_color = sample_catmull_rom(PreviousFrame, vUV - MV, registers.rt_metrics);
        #else
            mediump vec3 history_color = textureLod(PreviousFrame, vUV - MV, 0.0).rgb;
        #endif
    #else
        float depth = sample_nearest_depth_box(CurrentDepth, vUV, registers.rt_metrics.xy);
        vec4 clip = vec4(2.0 * vUV - 1.0, depth, 1.0);
        vec4 reproj_pos = registers.reproj * clip;
        #if REPROJECTION_CUBIC_HISTORY
            mediump vec3 history_color = sample_catmull_rom(PreviousFrame, reproj_pos.xy / reproj_pos.w, registers.rt_metrics);
        #else
            mediump vec3 history_color = textureProjLod(PreviousFrame, reproj_pos.xyw, 0.0).rgb;
        #endif
    #endif

    history_color = convert_input(history_color);

    mediump float lerp_factor = 0.1;
    #if REPROJECTION_CLAMP_HISTORY
        history_color = clamp_history_box(history_color, CurrentFrame, vUV, current);
        #if REPROJECTION_UNBIASED_LUMA
            lerp_factor *= unbiased_luma_weight(history_color, current);
        #endif
    #endif

    mediump vec3 out_color = mix(history_color, current, lerp_factor);
    Color = convert_to_output(out_color);
#else
    Color = textureLod(CurrentFrame, vUV, 0.0).rgb;
#endif
}