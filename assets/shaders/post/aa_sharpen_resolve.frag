#version 450
precision highp float;
precision highp int;

layout(set = 0, binding = 0) uniform mediump sampler2D uInput;
#if HISTORY
layout(set = 0, binding = 1) uniform mediump sampler2D uHistoryInput;
layout(set = 0, binding = 2) uniform sampler2D uDepth;
#endif
layout(location = 0) out mediump vec3 FragColor;
layout(location = 1) out mediump vec4 SaveFragColor;
layout(location = 0) in vec2 vUV;

layout(push_constant, std430) uniform Registers
{
    mat4 reproj;
    vec2 inv_resolution;
} registers;

#define YCgCo 1
#define CLAMP_HISTORY 1
#define CLAMP_VARIANCE 1
#include "reprojection.h"

void main()
{
    const mediump float sharpen = 0.25;

#if HORIZONTAL
    mediump vec3 c0 = textureLodOffset(uInput, vUV, 0.0, ivec2(-1, 0)).rgb;
    mediump vec3 c1 = textureLodOffset(uInput, vUV, 0.0, ivec2(+0, 0)).rgb;
    mediump vec3 c2 = textureLodOffset(uInput, vUV, 0.0, ivec2(+1, 0)).rgb;
    mediump vec3 c3 = textureLodOffset(uInput, vUV, 0.0, ivec2(+2, 0)).rgb;
#elif VERTICAL
    mediump vec3 c0 = textureLodOffset(uInput, vUV, 0.0, ivec2(0, -1)).rgb;
    mediump vec3 c1 = textureLodOffset(uInput, vUV, 0.0, ivec2(0, +0)).rgb;
    mediump vec3 c2 = textureLodOffset(uInput, vUV, 0.0, ivec2(0, +1)).rgb;
    mediump vec3 c3 = textureLodOffset(uInput, vUV, 0.0, ivec2(0, +2)).rgb;
#endif
    mediump vec3 tmp_sharpened_input = (0.5 + sharpen) * (c1 + c2) - sharpen * (c0 + c3);
    tmp_sharpened_input = clamp(tmp_sharpened_input, 0.0, 1.0);

#if CLAMP_HISTORY && HISTORY
    #if HORIZONTAL
        mediump vec3 h0 = textureLod(uInput, vUV + vec2(0.5, -1.0) * registers.inv_resolution, 0.0).rgb;
        mediump vec3 h1 = textureLod(uInput, vUV + vec2(0.5, +1.0) * registers.inv_resolution, 0.0).rgb;
    #elif VERTICAL
        mediump vec3 h0 = textureLod(uInput, vUV + vec2(-1.0, 0.5) * registers.inv_resolution, 0.0).rgb;
        mediump vec3 h1 = textureLod(uInput, vUV + vec2(+1.0, 0.5) * registers.inv_resolution, 0.0).rgb;
    #endif

    #if YCgCo
        c1 = RGB_to_YCgCo(c1);
        c2 = RGB_to_YCgCo(c2);
        h0 = RGB_to_YCgCo(h0);
        h1 = RGB_to_YCgCo(h1);
    #endif
#endif

#if YCgCo && HISTORY
    mediump vec3 sharpened_input = RGB_to_YCgCo(tmp_sharpened_input);
#else
    mediump vec3 sharpened_input = tmp_sharpened_input;
#endif

#if HISTORY
    #if HORIZONTAL
        float min_depth = min(textureLod(uDepth, vUV, 0.0).x, textureLodOffset(uDepth, vUV, 0.0, ivec2(1, 0)).x);
    #elif VERTICAL
        float min_depth = min(textureLod(uDepth, vUV, 0.0).x, textureLodOffset(uDepth, vUV, 0.0, ivec2(0, 1)).x);
    #endif
    vec4 clip = vec4(2.0 * vUV - 1.0, min_depth, 1.0);
    vec4 reproj_pos = registers.reproj * clip;
    mediump vec3 history_color = textureProjLod(uHistoryInput, reproj_pos.xyw, 0.0).rgb;
    #if YCgCo
        history_color = RGB_to_YCgCo(history_color);
    #endif
    #if CLAMP_HISTORY
        mediump vec3 clamped_history_color = clamp_history(history_color, c1, c2, h0, h1);
        #if CLAMP_VARIANCE
            mediump float history_variance = textureLod(uHistoryInput, vUV, 0.0).a;
            history_color = deflicker(history_color, clamped_history_color, history_variance);
        #else
            history_color = clamped_history_color;
        #endif
    #endif
    const mediump float lerp_factor = 0.5;
    mediump vec3 color = mix(history_color, sharpened_input, lerp_factor);
#else
    mediump vec3 color = sharpened_input;
#endif

#if YCgCo && HISTORY
    FragColor = YCgCo_to_RGB(color);
#else
    FragColor = color;
#endif

#if !CLAMP_VARIANCE || !HISTORY || !CLAMP_HISTORY
    const mediump float history_variance = 0.0;
#endif
    SaveFragColor = vec4(tmp_sharpened_input, history_variance);
}