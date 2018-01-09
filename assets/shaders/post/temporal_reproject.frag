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
    float depth = textureLod(CurrentDepth, vUV, 0.0).x;

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

            // If we end up clamping, we either have a ghosting scenario, in which we should just see this for a frame or two,
            // or, we have a persistent pattern of clamping, which can be observed as flickering, so dampen this quickly.
            #if YCgCo
                mediump float clamped_luma = clamped_history_color.x;
                mediump float history_luma = history_color.x;
            #else
                mediump float clamped_luma = dot(clamped_history_color, vec3(0.29, 0.60, 0.11));
                mediump float history_luma = dot(history_color, vec3(0.29, 0.60, 0.11));
            #endif

            mediump float clamp_ratio = max(max(clamped_luma, history_luma), 0.001) / max(min(clamped_luma, history_luma), 0.001);
            mediump float variance_delta = clamp_ratio > 1.25 ? 0.25 : -0.1;

            // Adapt the variance delta over time.
            history_color = mix(clamped_history_color, history_color, history_variance);
            history_variance += variance_delta;
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
