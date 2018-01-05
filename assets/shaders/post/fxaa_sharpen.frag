#version 450
precision highp float;
precision highp int;

layout(set = 0, binding = 0) uniform mediump sampler2D uInput;
#if HISTORY
layout(set = 0, binding = 1) uniform mediump sampler2D uHistoryInput;
layout(set = 0, binding = 2) uniform sampler2D uDepth;
#endif
layout(location = 0) out mediump vec3 FragColor;
layout(location = 1) out mediump vec3 SaveFragColor;
layout(location = 0) in vec2 vUV;

layout(push_constant, std430) uniform Registers
{
    mat4 reproj;
    vec2 inv_resolution;
} registers;

mediump vec3 RGB_to_YCgCo(mediump vec3 c)
{
    return vec3(
        0.25 * c.r + 0.5 * c.g + 0.25 * c.b,
        0.5 * c.g - 0.25 * c.r - 0.25 * c.b,
        0.5 * c.r - 0.5 * c.b);
}

mediump vec3 YCgCo_to_RGB(mediump vec3 c)
{
    mediump float tmp = c.x - c.y;
    return vec3(tmp + c.z, c.x + c.y, tmp - c.z);

    // c.x - c.y + c.z = [0.25, 0.5, 0.25] - [-0.25, 0.5, -0.25] + [0.5, 0.0, -0.5] = [1.0, 0.0, 0.0]
    // c.x + c.y       = [0.25, 0.5, 0.25] + [-0.25, 0.5, -0.25]                    = [0.0, 1.0, 0.0]
    // c.x - c.y - c.z = [0.25, 0.5, 0.25] - [-0.25, 0.5, -0.25] - [0.5, 0.0, -0.5] = [0.0, 0.0, 1.0]
}

#define YCgCo 1
#define CLAMP_HISTORY 1
#define CLAMP_AABB 1

mediump vec3 clamp_history(mediump vec3 color, mediump vec3 lo, mediump vec3 hi)
{
#if CLAMP_AABB
    mediump vec3 center = 0.5 * (lo + hi);
    mediump vec3 radius = max(0.5 * (hi - lo), vec3(0.0001));
    mediump vec3 v = color - center;
    mediump vec3 units = v / radius;
    mediump vec3 a_units = abs(units);
    mediump float max_unit = max(max(a_units.x, a_units.y), a_units.z);
    if (max_unit > 1.0)
        return center + v / max_unit;
    else
        return color;
#else
    return clamp(color, lo, hi);
#endif
}

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
        mediump vec3 rgb_lo = RGB_to_YCgCo(c1);
        mediump vec3 rgb_hi = rgb_lo;

        mediump vec3 c2_conv = RGB_to_YCgCo(c2);
        mediump vec3 h0_conv = RGB_to_YCgCo(h0);
        mediump vec3 h1_conv = RGB_to_YCgCo(h1);
        rgb_lo = min(rgb_lo, c2_conv);
        rgb_lo = min(rgb_lo, h0_conv);
        rgb_lo = min(rgb_lo, h1_conv);
        rgb_hi = max(rgb_hi, c2_conv);
        rgb_hi = max(rgb_hi, h0_conv);
        rgb_hi = max(rgb_hi, h1_conv);
    #else
        mediump vec3 rgb_lo = c1;
        mediump vec3 rgb_hi = rgb_lo;
        rgb_lo = min(rgb_lo, c2);
        rgb_lo = min(rgb_lo, h0);
        rgb_lo = min(rgb_lo, h1);
        rgb_hi = max(rgb_hi, c2);
        rgb_hi = max(rgb_hi, h0);
        rgb_hi = max(rgb_hi, h1);
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
        history_color = clamp_history(history_color, rgb_lo, rgb_hi);
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
    SaveFragColor = tmp_sharpened_input;
}