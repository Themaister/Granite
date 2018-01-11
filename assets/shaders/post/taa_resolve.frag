#version 450
precision highp float;
precision highp int;

#define YCgCo 1
#define HDR 1
#define CLAMP_HISTORY 1
#define UNBIASED_LUMA 1
#define CUBIC_HISTORY 1

layout(set = 0, binding = 0) uniform mediump sampler2D CurrentFrame;
#if HISTORY
layout(set = 0, binding = 1) uniform sampler2D CurrentDepth;
layout(set = 0, binding = 2) uniform mediump sampler2D PreviousFrame;
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
#if HISTORY
    mediump vec3 current = SAMPLE_CURRENT(CurrentFrame, vUV, 0, 0);

    float depth = sample_min_depth_box(CurrentDepth, vUV, registers.rt_metrics.xy);
    vec4 clip = vec4(2.0 * vUV - 1.0, depth, 1.0);
    vec4 reproj_pos = registers.reproj * clip;

    #if CUBIC_HISTORY
        mediump vec3 history_color = sample_catmull_rom(PreviousFrame, reproj_pos.xy / reproj_pos.w, registers.rt_metrics);
    #else
        mediump vec3 history_color = textureProjLod(PreviousFrame, reproj_pos.xyw, 0.0).rgb;
    #endif

    #if HDR
        history_color = Tonemap(history_color);
    #endif
    #if YCgCo
        history_color = RGB_to_YCgCo(history_color);
    #endif

    mediump float lerp_factor = 0.1;

    #if CLAMP_HISTORY
        history_color = clamp_history_box(history_color, CurrentFrame, vUV, current);
        #if UNBIASED_LUMA
            // Adjust lerp factor.
            #if YCgCo
                mediump float clamped_luma = history_color.x;
                mediump float current_luma = current.x;
            #else
                mediump float clamped_luma = luminance(history_color);
                mediump float current_luma = luminance(current);
            #endif
            mediump float diff = abs(current_luma - clamped_luma) / max(current_luma, max(clamped_luma, 0.001));
            diff = 1.0 - diff;
            lerp_factor *= 0.9 * diff * diff + 0.1;
        #endif
    #endif

    mediump vec3 out_color = mix(history_color, current, lerp_factor);
    #if YCgCo
        out_color = clamp(YCgCo_to_RGB(out_color), 0.0, 0.999);
    #endif
    #if HDR
        out_color = TonemapInvert(out_color);
    #endif
    Color = out_color;
#else
    Color = textureLod(CurrentFrame, vUV, 0.0).rgb;
#endif
}