#ifndef REPROJECTION_H_
#define REPROJECTION_H_

mediump float RCP(mediump float v) { return 1.0 / v; }
mediump float Max3(mediump float x, mediump float y, mediump float z) { return max(x, max(y, z)); }
mediump vec3 Tonemap(mediump vec3 c) { return c * RCP(Max3(c.r, c.g, c.b) + 1.0); }
mediump vec3 TonemapInvert(mediump vec3 c) { return c * RCP(1.0 - Max3(c.r, c.g, c.b)); }

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

#define SAMPLE_CURRENT(tex, uv, x, y) \
    (textureLodOffset(tex, uv, 0.0, ivec2(x, y)).rgb)

#define VARIANCE_CLIPPING 1

mediump vec3 clamp_history_box(mediump vec3 history_color,
                               mediump sampler2D CurrentFrame,
                               vec2 UV,
                               mediump vec3 c11, inout mediump float lerp_factor)
{
    mediump vec3 c01 = SAMPLE_CURRENT(CurrentFrame, UV, -1, 0);
    mediump vec3 c21 = SAMPLE_CURRENT(CurrentFrame, UV, +1, 0);
    mediump vec3 c10 = SAMPLE_CURRENT(CurrentFrame, UV, 0, -1);
    mediump vec3 c12 = SAMPLE_CURRENT(CurrentFrame, UV, 0, +1);

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

    mediump vec3 c00 = SAMPLE_CURRENT(CurrentFrame, UV, -1, -1);
    mediump vec3 c22 = SAMPLE_CURRENT(CurrentFrame, UV, +1, +1);
    mediump vec3 c02 = SAMPLE_CURRENT(CurrentFrame, UV, -1, +1);
    mediump vec3 c20 = SAMPLE_CURRENT(CurrentFrame, UV, +1, -1);

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

#if VARIANCE_CLIPPING
    vec3 m1 = (c00 + c01 + c02 + c10 + c11 + c12 + c20 + c21 + c22) / 9.0;
    vec3 m2 =
        c00 * c00 + c01 * c01 + c02 * c02 +
        c10 * c10 + c11 * c11 + c12 * c12 +
        c20 * c20 + c21 * c21 + c22 * c22;
    vec3 sigma = sqrt(max(m2 / 9.0 - m1 * m1, 0.0));
    const float gamma = 1.0;
    lo_cross = max(lo_cross, m1 - gamma * sigma);
    hi_cross = min(hi_cross, m1 + gamma * sigma);
#endif

#if 1
    // Adjust lerp factor.
    mediump float diff = abs(history_color.x - c11.x) / max(c11.x, max(history_color.x, max(0.0001, hi_cross.x)));
    diff = 1.0 - diff;
    lerp_factor *= diff * diff;
#endif

    return clamp_box(history_color, lo_cross, hi_cross);
}

mediump vec3 deflicker(mediump vec3 history_color, mediump vec3 clamped_history, inout mediump float history_variance)
{
    // If we end up clamping, we either have a ghosting scenario, in which we should just see this for a frame or two,
    // or, we have a persistent pattern of clamping, which can be observed as flickering, so dampen this quickly.
    #if YCgCo
        mediump float clamped_luma = clamped_history.x;
        mediump float history_luma = history_color.x;
    #else
        mediump float clamped_luma = dot(clamped_history, vec3(0.29, 0.60, 0.11));
        mediump float history_luma = dot(history_color, vec3(0.29, 0.60, 0.11));
    #endif

    mediump vec3 result = mix(clamped_history, history_color, history_variance);

    // Adapt the variance delta over time.
    mediump float clamp_ratio = max(max(clamped_luma, history_luma), 0.001) / max(min(clamped_luma, history_luma), 0.001);
    history_variance += 4.0 * clamp(clamp_ratio - 1.25, 0.0, 0.35) - 0.1;
    return result;
}

float sample_min_depth_box(sampler2D Depth, vec2 UV, vec2 inv_resolution)
{
	// Sample nearest "velocity" from cross.
    // We reproject using depth buffer instead here.
    vec2 ShiftUV = UV - 0.5 * inv_resolution;
    vec3 quad0 = textureGather(Depth, ShiftUV).xyz;
    vec2 quad1 = textureGatherOffset(Depth, ShiftUV, ivec2(1)).xz;
    vec2 min0 = min(quad0.xy, quad1);
    float result = min(min0.x, min0.y);
    return min(result, quad0.z);
}
#endif