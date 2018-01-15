#version 450
precision highp float;
precision highp int;

layout(set = 0, binding = 0) uniform mediump sampler2D uInput;
#if REPROJECTION_HISTORY
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

#define REPROJECTION_YCgCo 1
#define REPROJECTION_HDR 0
#define REPROJECTION_CLAMP_HISTORY 1
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

#if REPROJECTION_CLAMP_HISTORY && REPROJECTION_HISTORY
    #if HORIZONTAL
        mediump vec3 h0 = textureLod(uInput, vUV + vec2(0.5, -1.0) * registers.inv_resolution, 0.0).rgb;
        mediump vec3 h1 = textureLod(uInput, vUV + vec2(0.5, +1.0) * registers.inv_resolution, 0.0).rgb;
    #elif VERTICAL
        mediump vec3 h0 = textureLod(uInput, vUV + vec2(-1.0, 0.5) * registers.inv_resolution, 0.0).rgb;
        mediump vec3 h1 = textureLod(uInput, vUV + vec2(+1.0, 0.5) * registers.inv_resolution, 0.0).rgb;
    #endif

    c1 = convert_input(c1);
    c2 = convert_input(c2);
    h0 = convert_input(h0);
    h1 = convert_input(h1);
#endif

    mediump vec3 sharpened_input = convert_input(tmp_sharpened_input);

#if REPROJECTION_HISTORY
    #if HORIZONTAL
        // 2x3 region.
        vec2 base_uv = vUV + vec2(0.5, -0.5) * registers.inv_resolution;
        vec4 quad0 = textureGather(uDepth, base_uv, 0);
        vec2 quad1 = textureGatherOffset(uDepth, base_uv, ivec2(0, 1), 0).xy;
        vec2 min_depth0 = min(quad0.xy, quad0.zw);
        min_depth0 = min(min_depth0, quad1);
        float min_depth = min(min_depth0.x, min_depth0.y);
    #elif VERTICAL
        // 3x2 region.
        vec2 base_uv = vUV + vec2(-0.5, 0.5) * registers.inv_resolution;
        vec4 quad0 = textureGather(uDepth, base_uv, 0);
        vec2 quad1 = textureGatherOffset(uDepth, base_uv, ivec2(1, 0), 0).yz;
        vec2 min_depth0 = min(quad0.xy, quad0.zw);
        min_depth0 = min(min_depth0, quad1);
        float min_depth = min(min_depth0.x, min_depth0.y);
    #endif
    vec4 clip = vec4(2.0 * vUV - 1.0, min_depth, 1.0);
    vec4 reproj_pos = registers.reproj * clip;
    mediump vec4 history_color_variance = textureProjLod(uHistoryInput, reproj_pos.xyw, 0.0);
    mediump vec3 history_color = convert_input(history_color_variance.rgb);
    mediump float variance = history_color_variance.a;
    #if REPROJECTION_CLAMP_HISTORY
        mediump vec3 clamped_history_color = clamp_history4(history_color, c1, c2, h0, h1);
        mediump float clamped_luma = luminance(clamped_history_color);
        mediump float history_luma = luminance(history_color);
        mediump float variance_delta = abs(clamped_luma - history_luma) / max(max(clamped_luma, history_luma), 0.002);
        variance_delta = clamp(variance_delta - 0.15, -0.15, 0.3);

        history_color = mix(clamped_history_color, history_color, variance);
        variance += variance_delta;
    #endif
    const mediump float lerp_factor = 0.5;
    mediump vec3 color = mix(history_color, sharpened_input, lerp_factor);
    FragColor = convert_to_output(color);
    SaveFragColor = vec4(tmp_sharpened_input, variance);
#else
    FragColor = tmp_sharpened_input;
    SaveFragColor = vec4(tmp_sharpened_input, 0.0);
#endif
}