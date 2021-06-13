#version 450
precision highp float;
precision highp int;

#if TAA_QUALITY == 0
    #define REPROJECTION_CUBIC_HISTORY 0
    #define REPROJECTION_CLAMP_METHOD REPROJECTION_CLAMP_METHOD_CLAMP
    #define NEIGHBOR_METHOD NEIGHBOR_METHOD_5TAP_CROSS
    #define NEAREST_METHOD NEAREST_METHOD_5TAP_CROSS
#elif TAA_QUALITY == 1
    #define REPROJECTION_CUBIC_HISTORY 0
    #define REPROJECTION_CLAMP_METHOD REPROJECTION_CLAMP_METHOD_AABB
    #define NEIGHBOR_METHOD NEIGHBOR_METHOD_ROUNDED_CORNER
    #define NEAREST_METHOD NEAREST_METHOD_5TAP_CROSS
#elif TAA_QUALITY == 2
    #define REPROJECTION_CUBIC_HISTORY 1
    #define REPROJECTION_CLAMP_METHOD REPROJECTION_CLAMP_METHOD_AABB
    #define NEIGHBOR_METHOD NEIGHBOR_METHOD_VARIANCE
    #define NEAREST_METHOD NEAREST_METHOD_3x3
#else
#error "Unknown TAA quality."
#endif

layout(set = 0, binding = 0) uniform mediump sampler2D CurrentFrame;
#if REPROJECTION_HISTORY
layout(set = 0, binding = 1) uniform sampler2D CurrentDepth;
layout(set = 0, binding = 2) uniform sampler2D MVs;
layout(set = 0, binding = 3) uniform mediump sampler2D PreviousFrame;
#endif

#include "reprojection.h"

layout(std430, push_constant) uniform Registers
{
    mat4 reproj;
    vec4 rt_metrics;
} registers;

layout(location = 0) in vec2 vUV;
layout(location = 0) out mediump vec3 Color;
layout(location = 1) out mediump vec3 HistoryColor;

void main()
{
    mediump vec3 current = SAMPLE_CURRENT(CurrentFrame, vUV, 0, 0);
#if REPROJECTION_HISTORY
    vec3 MV_d = sample_nearest_velocity(CurrentDepth, MVs, vUV, registers.rt_metrics.xy);
    vec2 oldUV;

    if (all(equal(MV_d.xy, vec2(0.0))))
    {
        // Closest depth sample did not have explicit motion, reproject from camera.
        float depth = MV_d.z;
        vec4 clip = vec4(2.0 * vUV - 1.0, depth, 1.0);
        vec4 reproj_pos = registers.reproj * clip;
        oldUV = reproj_pos.xy / reproj_pos.w;
        MV_d.xy = vUV - oldUV;
    }
    else
        oldUV = vUV - MV_d.xy;

    #if REPROJECTION_CUBIC_HISTORY
        mediump vec3 history_color = sample_catmull_rom(PreviousFrame, oldUV, registers.rt_metrics);
    #else
        mediump vec3 history_color = textureLod(PreviousFrame, oldUV, 0.0).rgb;
    #endif

    mediump float MV_length = length(MV_d.xy);
    mediump float MV_fast = min(MV_length * 50.0, 1.0);
    mediump float gamma = mix(1.5, 0.5, MV_fast);

    history_color = clamp(history_color, vec3(0.0, -1.0, -1.0), vec3(1.0));
    mediump float lerp_factor = (1.0 + 2.0 * MV_fast) / 16.0;
    history_color = clamp_history_box(history_color, CurrentFrame, vUV, current, gamma);

    mediump vec3 out_color = mix(history_color, current, lerp_factor);
    HistoryColor = out_color;
    Color = TAAToHDRColorSpace(out_color);
#else
    Color = TAAToHDRColorSpace(current);
    HistoryColor = current;
#endif
}
