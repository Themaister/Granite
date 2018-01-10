#version 450
precision highp float;
precision highp int;

#define YCgCo 1
#define CLAMP_HISTORY 1
#define CLAMP_VARIANCE 0
#define HDR 1

layout(set = 0, binding = 0) uniform mediump sampler2D CurrentFrame;
#if HISTORY
layout(set = 0, binding = 1) uniform sampler2D CurrentDepth;
layout(set = 0, binding = 2) uniform mediump sampler2D PreviousFrame;
#endif

layout(std430, push_constant) uniform Registers
{
    mat4 reproj;
    vec2 inv_resolution;
} registers;

layout(location = 0) in vec2 vUV;
layout(location = 0) out mediump vec4 Color;
#include "reprojection.h"

void main()
{
#if HISTORY
    mediump vec3 current = SAMPLE_CURRENT(CurrentFrame, vUV, 0, 0);

    float depth = sample_min_depth_box(CurrentDepth, vUV, registers.inv_resolution);
    vec4 clip = vec4(2.0 * vUV - 1.0, depth, 1.0);
    vec4 reproj_pos = registers.reproj * clip;
    mediump vec3 history_color = textureProjLod(PreviousFrame, reproj_pos.xyw, 0.0).rgb;
    mediump float history_variance = textureLod(PreviousFrame, vUV, 0.0).a;
    #if HDR
        history_color = Tonemap(history_color);
    #endif
    #if YCgCo
        history_color = RGB_to_YCgCo(history_color);
    #endif

    mediump float lerp_factor = 0.1;

    #if CLAMP_HISTORY
        mediump vec3 clamped_history_color = clamp_history_box(history_color, CurrentFrame, vUV, current, lerp_factor);
        #if CLAMP_VARIANCE
            history_color = deflicker(history_color, clamped_history_color, history_variance);
        #else
            history_color = clamped_history_color;
        #endif
    #endif

    mediump vec3 out_color = mix(history_color, current, lerp_factor);
    #if YCgCo
        out_color = clamp(YCgCo_to_RGB(out_color), 0.0, 0.999);
    #endif
    #if HDR
        out_color = TonemapInvert(out_color);
    #endif
    history_variance = clamp(history_variance, 0.0, 1.0);
    Color = vec4(out_color, history_variance);
#else
    mediump vec3 converted = SAMPLE_CURRENT(CurrentFrame, vUV, 0, 0);
    #if YCgCo
        converted = YCgCo_to_RGB(converted);
    #endif
    #if HDR
        converted = TonemapInvert(converted);
    #endif
    Color = vec4(converted, 0.0);
#endif
}