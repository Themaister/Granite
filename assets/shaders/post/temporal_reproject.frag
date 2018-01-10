#version 450
precision highp float;
precision highp int;

#define YCgCo 1
#define CLAMP_HISTORY 1
#define CLAMP_VARIANCE 1

layout(set = 0, binding = 0) uniform sampler2D CurrentFrame;
#if HISTORY
layout(set = 0, binding = 1) uniform sampler2D PreviousFrame;
layout(set = 0, binding = 2) uniform sampler2D CurrentDepth;
#if CLAMP_VARIANCE
layout(set = 0, binding = 3) uniform sampler2D LastVariance;
#endif
#endif

layout(std430, push_constant) uniform Registers
{
    mat4 reproj;
    vec2 inv_resolution;
} registers;

layout(location = 0) in vec2 vUV;
layout(location = 0) out mediump vec3 Color;
#if CLAMP_VARIANCE
layout(location = 1) out mediump float Variance;
#endif
#include "reprojection.h"

void main()
{
#if HISTORY
    mediump vec3 current = textureLod(CurrentFrame, vUV, 0.0).rgb;

    float depth = sample_min_depth_box(CurrentDepth, vUV, registers.inv_resolution);

    vec4 clip = vec4(2.0 * vUV - 1.0, depth, 1.0);
    vec4 reproj_pos = registers.reproj * clip;
    mediump vec3 history_color = textureProjLod(PreviousFrame, reproj_pos.xyw, 0.0).rgb;
    #if YCgCo
        history_color = RGB_to_YCgCo(history_color);
        current = RGB_to_YCgCo(current);
    #endif

    #if CLAMP_HISTORY
        mediump vec3 clamped_history_color = clamp_history_box(history_color, CurrentFrame, vUV, current);
        #if CLAMP_VARIANCE
            mediump float history_variance = textureLod(LastVariance, vUV, 0.0).x;
            history_color = deflicker(history_color, clamped_history_color, history_variance);
        #else
            history_color = clamped_history_color;
        #endif
    #endif

    const mediump float lerp_factor = 0.5;
    Color = mix(history_color, current, lerp_factor);
    #if YCgCo
        Color = YCgCo_to_RGB(Color);
    #endif
#else
    Color = textureLod(CurrentFrame, vUV, 0.0).rgb; // Only happens first frame, so whatever.
#endif

#if !HISTORY || !CLAMP_HISTORY
    const mediump float history_variance = 0.0;
#endif
#if CLAMP_VARIANCE
    Variance = history_variance;
#endif
}
