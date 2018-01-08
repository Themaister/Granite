#version 450
precision highp float;
precision highp int;

layout(set = 0, binding = 0) uniform sampler2D CurrentFrame;
#if HISTORY
layout(set = 0, binding = 1) uniform sampler2D PreviousFrame;
layout(set = 0, binding = 2) uniform sampler2D CurrentDepth;
#endif

layout(std430, push_constant) uniform Registers
{
    mat4 reproj;
} registers;

layout(location = 0) in vec2 vUV;
layout(location = 0) out mediump vec3 Color;

#define YCgCo 1
#define CLAMP_HISTORY 1
#include "reprojection.h"

void main()
{
#if HISTORY
    mediump vec3 c0 = textureLodOffset(CurrentFrame, vUV, 0.0, ivec2(-1, 0)).rgb;
    mediump vec3 c1 = textureLodOffset(CurrentFrame, vUV, 0.0, ivec2(+1, 0)).rgb;
    mediump vec3 c2 = textureLodOffset(CurrentFrame, vUV, 0.0, ivec2(0, -1)).rgb;
    mediump vec3 c3 = textureLodOffset(CurrentFrame, vUV, 0.0, ivec2(0, +1)).rgb;
    mediump vec3 current = textureLod(CurrentFrame, vUV, 0.0).rgb;
    float depth = textureLod(CurrentDepth, vUV, 0.0).x;

    #if YCgCo
        c0 = RGB_to_YCgCo(c0);
        c1 = RGB_to_YCgCo(c1);
        c2 = RGB_to_YCgCo(c2);
        c3 = RGB_to_YCgCo(c3);
    #endif

    vec4 clip = vec4(2.0 * vUV - 1.0, depth, 1.0);
    vec4 reproj_pos = registers.reproj * clip;
    mediump vec3 history_color = textureProjLod(PreviousFrame, reproj_pos.xyw, 0.0).rgb;
    #if YCgCo
        history_color = RGB_to_YCgCo(history_color);
    #endif
    #if CLAMP_HISTORY
        history_color = clamp_history(history_color, c0, c1, c2, c3);
    #endif
    #if YCgCo
        history_color = YCgCo_to_RGB(history_color);
    #endif
    const mediump float lerp_factor = 0.5;
    Color = mix(history_color, current, lerp_factor);
#else
    Color = textureLod(CurrentFrame, vUV, 0.0).rgb; // Only happens first frame, so whatever.
#endif
}
