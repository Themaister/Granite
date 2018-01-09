#ifndef REPROJECTION_H_
#define REPROJECTION_H_

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

#define CLAMP_AABB 1
mediump vec3 clamp_box(mediump vec3 color, mediump vec3 lo, mediump vec3 hi)
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

mediump vec3 clamp_history(mediump vec3 color,
                           mediump vec3 c0,
                           mediump vec3 c1,
                           mediump vec3 c2,
                           mediump vec3 c3)
{
    mediump vec3 lo = c0;
    mediump vec3 hi = c0;
    lo = min(lo, c1);
    lo = min(lo, c2);
    lo = min(lo, c3);
    hi = max(hi, c1);
    hi = max(hi, c2);
    hi = max(hi, c3);
    return clamp_box(color, lo, hi);
}

mediump vec3 clamp_history(mediump vec3 color,
                           mediump vec3 c0,
                           mediump vec3 c1,
                           mediump vec3 c2,
                           mediump vec3 c3,
                           mediump vec3 c4)
{
    mediump vec3 lo = c0;
    mediump vec3 hi = c0;
    lo = min(lo, c1);
    lo = min(lo, c2);
    lo = min(lo, c3);
    lo = min(lo, c4);
    hi = max(hi, c1);
    hi = max(hi, c2);
    hi = max(hi, c3);
    hi = max(hi, c4);
    return clamp_box(color, lo, hi);
}

mediump vec3 clamp_history_box(mediump vec3 history_color,
                               mediump sampler2D CurrentFrame,
                               vec2 UV,
                               mediump vec3 c11)
{
    mediump vec3 c01 = textureLodOffset(CurrentFrame, vUV, 0.0, ivec2(-1, 0)).rgb;
    mediump vec3 c21 = textureLodOffset(CurrentFrame, vUV, 0.0, ivec2(+1, 0)).rgb;
    mediump vec3 c10 = textureLodOffset(CurrentFrame, vUV, 0.0, ivec2(0, -1)).rgb;
    mediump vec3 c12 = textureLodOffset(CurrentFrame, vUV, 0.0, ivec2(0, +1)).rgb;
#if YCgCo
    c01 = RGB_to_YCgCo(c01);
    c21 = RGB_to_YCgCo(c21);
    c10 = RGB_to_YCgCo(c10);
    c12 = RGB_to_YCgCo(c12);
#endif
    mediump vec3 lo_cross = c11;
    mediump vec3 hi_cross = c11;
    lo_cross = min(lo_cross, c01);
    lo_cross = min(lo_cross, c21);
    lo_cross = min(lo_cross, c10);
    lo_cross = min(lo_cross, c12);
    hi_cross = max(hi_cross, c01);
    hi_cross = max(hi_cross, c21);
    hi_cross = max(hi_cross, c10);
    hi_cross = max(hi_cross, c12);

    mediump vec3 c00 = textureLodOffset(CurrentFrame, vUV, 0.0, ivec2(-1, -1)).rgb;
    mediump vec3 c22 = textureLodOffset(CurrentFrame, vUV, 0.0, ivec2(+1, +1)).rgb;
    mediump vec3 c02 = textureLodOffset(CurrentFrame, vUV, 0.0, ivec2(-1, +1)).rgb;
    mediump vec3 c20 = textureLodOffset(CurrentFrame, vUV, 0.0, ivec2(+1, -1)).rgb;
#if YCgCo
    c00 = RGB_to_YCgCo(c00);
    c22 = RGB_to_YCgCo(c22);
    c02 = RGB_to_YCgCo(c02);
    c20 = RGB_to_YCgCo(c20);
#endif

    mediump vec3 lo_box = lo_cross;
    mediump vec3 hi_box = hi_cross;
    lo_box = min(lo_box, c00);
    lo_box = min(lo_box, c22);
    lo_box = min(lo_box, c02);
    lo_box = min(lo_box, c20);
    hi_box = max(hi_box, c00);
    hi_box = max(hi_box, c22);
    hi_box = max(hi_box, c02);
    hi_box = max(hi_box, c20);
    lo_cross = mix(lo_cross, lo_box, 0.5);
    hi_cross = mix(hi_cross, hi_box, 0.5);

    return clamp_box(history_color, lo_cross, hi_cross);
}

#endif