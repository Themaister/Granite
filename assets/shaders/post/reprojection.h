#ifndef REPROJECTION_H_
#define REPROJECTION_H_

#ifndef REPROJECTION_HDR
#define REPROJECTION_HDR 1
#endif
#ifndef REPROJECTION_YCgCo
#define REPROJECTION_YCgCo 1
#endif

#define REPROJECTION_CLAMP_METHOD_AABB 0
#define REPROJECTION_CLAMP_METHOD_CLAMP 1
#ifndef REPROJECTION_CLAMP_METHOD
#define REPROJECTION_CLAMP_METHOD REPROJECTION_CLAMP_METHOD_AABB
#endif

#ifndef REPROJECTION_VARIANCE_CLIPPING
#define REPROJECTION_VARIANCE_CLIPPING 1
#endif

#define NEIGHBOR_METHOD_5TAP_CROSS 0
#define NEIGHBOR_METHOD_5TAP_DIAMOND 1
#define NEIGHBOR_METHOD_3x3 2
#define NEIGHBOR_METHOD_ROUNDED_CORNER 3
#define NEIGHBOR_METHOD_ROUNDED_CORNER_VARIANCE 4
#define NEIGHBOR_METHOD_VARIANCE 5
#ifndef NEIGHBOR_METHOD
#define NEIGHBOR_METHOD NEIGHBOR_METHOD_ROUNDED_CORNER_VARIANCE
#endif

#define NEAREST_METHOD_5TAP_CROSS 0
#define NEAREST_METHOD_5TAP_DIAMOND 1
#define NEAREST_METHOD_3x3 2
#ifndef NEAREST_METHOD
#define NEAREST_METHOD NEAREST_METHOD_3x3
#endif

// max3 based tonemapper.
mediump float RCP(mediump float v)
{
    return 1.0 / v;
}

mediump float Max3(mediump float x, mediump float y, mediump float z)
{
    return max(x, max(y, z));
}

mediump vec3 Tonemap(mediump vec3 c)
{
    return c * RCP(Max3(c.r, c.g, c.b) + 1.0);
}

mediump vec3 TonemapInvert(mediump vec3 c)
{
    return c * RCP(1.0 - Max3(c.r, c.g, c.b));
}

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

mediump vec3 clamp_box(mediump vec3 color, mediump vec3 lo, mediump vec3 hi)
{
#if REPROJECTION_CLAMP_METHOD == REPROJECTION_CLAMP_METHOD_AABB
    mediump vec3 center = 0.5 * (lo + hi);
    mediump vec3 radius = max(0.5 * (hi - lo), vec3(0.0001));
    mediump vec3 v = color - center;
    mediump vec3 units = v / radius;
    mediump vec3 a_units = abs(units);
    mediump float max_unit = max(max(a_units.x, a_units.y), a_units.z);
	mediump vec3 result;
    if (max_unit > 1.0)
        result = center + v / max_unit;
    else
        result = color;
	return result;
#elif REPROJECTION_CLAMP_METHOD == REPROJECTION_CLAMP_METHOD_CLAMP
    return clamp(color, lo, hi);
#else
#error "No clamp method selected."
#endif
}

mediump vec3 clamp_history4(
        mediump vec3 color,
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

mediump vec3 convert_input(mediump vec3 color)
{
#if REPROJECTION_HDR && REPROJECTION_YCgCo
    return RGB_to_YCgCo(Tonemap(color));
#elif REPROJECTION_HDR
    return Tonemap(color);
#elif REPROJECTION_YCgCo
    return RGB_to_YCgCo(color);
#else
    return color;
#endif
}

#define SAMPLE_CURRENT(tex, uv, x, y) convert_input(textureLodOffset(tex, uv, 0.0, ivec2(x, y)).rgb)

mediump vec3 convert_to_output(mediump vec3 color)
{
#if REPROJECTION_HDR && REPROJECTION_YCgCo
    return TonemapInvert(clamp(YCgCo_to_RGB(color), 0.0, 0.999));
#elif REPROJECTION_HDR
    return TonemapInvert(color);
#elif REPROJECTION_YCgCo
    return YCgCo_to_RGB(color);
#else
    return color;
#endif
}

#if NEIGHBOR_METHOD == NEIGHBOR_METHOD_5TAP_CROSS
	#define NEED_CROSS 1
	#define NEED_DIAMOND 0
	#define NEED_MINMAX 1
	#define NEED_CORNER_ROUNDING 0
	#define NEED_VARIANCE 0
#elif NEIGHBOR_METHOD == NEIGHBOR_METHOD_5TAP_DIAMOND
	#define NEED_CROSS 0
	#define NEED_DIAMOND 1
	#define NEED_MINMAX 1
	#define NEED_CORNER_ROUNDING 0
	#define NEED_VARIANCE 0
#elif NEIGHBOR_METHOD == NEIGHBOR_METHOD_3x3
	#define NEED_CROSS 1
	#define NEED_DIAMOND 1
	#define NEED_MINMAX 1
	#define NEED_CORNER_ROUNDING 0
	#define NEED_VARIANCE 0
#elif NEIGHBOR_METHOD == NEIGHBOR_METHOD_ROUNDED_CORNER
	#define NEED_CROSS 1
	#define NEED_DIAMOND 1
	#define NEED_MINMAX 1
	#define NEED_CORNER_ROUNDING 1
	#define NEED_VARIANCE 0
#elif NEIGHBOR_METHOD == NEIGHBOR_METHOD_ROUNDED_CORNER_VARIANCE
	#define NEED_CROSS 1
	#define NEED_DIAMOND 1
	#define NEED_MINMAX 1
	#define NEED_CORNER_ROUNDING 1
	#define NEED_VARIANCE 1
#elif NEIGHBOR_METHOD == NEIGHBOR_METHOD_VARIANCE
	#define NEED_CROSS 1
	#define NEED_DIAMOND 1
	#define NEED_MINMAX 0
	#define NEED_CORNER_ROUNDING 0
	#define NEED_VARIANCE 1
#else
#error "Unknown neighbor method."
#endif

mediump vec3 clamp_history_box(mediump vec3 history_color,
                               mediump sampler2D Current,
                               vec2 UV,
                               mediump vec3 c11)
{
#if NEED_MINMAX
	mediump vec3 lo = c11;
	mediump vec3 hi = c11;
#endif

#if NEED_CROSS
    mediump vec3 c01 = SAMPLE_CURRENT(Current, UV, -1, 0);
    mediump vec3 c21 = SAMPLE_CURRENT(Current, UV, +1, 0);
    mediump vec3 c10 = SAMPLE_CURRENT(Current, UV, 0, -1);
    mediump vec3 c12 = SAMPLE_CURRENT(Current, UV, 0, +1);
    lo = min(lo, c01);
    lo = min(lo, c21);
    lo = min(lo, c10);
    lo = min(lo, c12);
    hi = max(hi, c01);
    hi = max(hi, c21);
    hi = max(hi, c10);
    hi = max(hi, c12);
#endif

#if NEED_CORNER_ROUNDING
    mediump vec3 corner_lo = lo;
	mediump vec3 corner_hi = hi;
#endif

#if NEED_DIAMOND
    mediump vec3 c00 = SAMPLE_CURRENT(Current, UV, -1, -1);
    mediump vec3 c22 = SAMPLE_CURRENT(Current, UV, +1, +1);
    mediump vec3 c02 = SAMPLE_CURRENT(Current, UV, -1, +1);
    mediump vec3 c20 = SAMPLE_CURRENT(Current, UV, +1, -1);
    lo = min(lo, c00);
    lo = min(lo, c22);
    lo = min(lo, c02);
    lo = min(lo, c20);
    hi = max(hi, c00);
    hi = max(hi, c22);
    hi = max(hi, c02);
    hi = max(hi, c20);
#endif

#if NEED_CORNER_ROUNDING
	lo = 0.5 * (corner_lo + lo);
	hi = 0.5 * (corner_hi + hi);
#endif

#if NEED_VARIANCE
    vec3 m1 = (c00 + c01 + c02 + c10 + c11 + c12 + c20 + c21 + c22) / 9.0;
    vec3 m2 =
        c00 * c00 + c01 * c01 + c02 * c02 +
        c10 * c10 + c11 * c11 + c12 * c12 +
        c20 * c20 + c21 * c21 + c22 * c22;
    vec3 sigma = sqrt(max(m2 / 9.0 - m1 * m1, 0.0));
    const float gamma = 1.0;
	#if NEED_MINMAX
		lo = max(lo, m1 - gamma * sigma);
		hi = min(hi, m1 + gamma * sigma);
	#else
		mediump vec3 lo = m1 - gamma * sigma;
		mediump vec3 hi = m1 + gamma * sigma;
	#endif
#endif

    return clamp_box(history_color, lo, hi);
}

mediump float luminance(mediump vec3 color)
{
#if REPROJECTION_YCgCo
    return color.x;
#else
	return dot(color, vec3(0.29, 0.60, 0.11));
#endif
}

mediump float unbiased_luma_weight(mediump vec3 history, mediump vec3 current)
{
	// Adjust lerp factor.
	mediump float clamped_luma = luminance(history);
	mediump float current_luma = luminance(current);
	mediump float diff = abs(current_luma - clamped_luma) / max(current_luma, max(clamped_luma, 0.001));
	diff = 1.0 - diff;
	return 0.99 * diff * diff + 0.01;
}

float sample_nearest_depth_box(sampler2D Depth, vec2 UV, vec2 inv_resolution)
{
#if NEAREST_METHOD == NEAREST_METHOD_5TAP_CROSS
	// Sample nearest "velocity" from cross.
    // We reproject using depth buffer instead here.
    vec2 ShiftUV = UV - 0.5 * inv_resolution;
    vec3 quad0 = textureGather(Depth, ShiftUV).xyz;
    vec2 quad1 = textureGatherOffset(Depth, ShiftUV, ivec2(1)).xz;
    vec2 min0 = min(quad0.xy, quad1);
    float result = min(min0.x, min0.y);
    return min(result, quad0.z);
#elif NEAREST_METHOD == NEAREST_METHOD_5TAP_DIAMOND
	float d0 = textureLodOffset(Depth, UV, 0.0, ivec2(-1, -1)).x;
	float d1 = textureLodOffset(Depth, UV, 0.0, ivec2(+1, -1)).x;
	float d2 = textureLodOffset(Depth, UV, 0.0, ivec2(-1, +1)).x;
	float d3 = textureLodOffset(Depth, UV, 0.0, ivec2(+1, +1)).x;
	float d4 = textureLod(Depth, UV, 0.0).x;
	return min(d4, min(min(d0, d1), min(d2, d3)));
#elif NEAREST_METHOD == NEAREST_METHOD_3x3
    vec2 ShiftUV = UV - 0.5 * inv_resolution;
    vec4 quad0 = textureGather(Depth, ShiftUV, 0);
    vec2 quad1 = textureGatherOffset(Depth, ShiftUV, ivec2(1, 0), 0).yz;
    vec2 quad2 = textureGatherOffset(Depth, ShiftUV, ivec2(0, 1), 0).xy;
    float quad3 = textureLodOffset(Depth, UV, 0.0, ivec2(1)).x;
	vec4 m0 = min(quad0, vec4(quad1, quad2));
    vec2 m1 = min(m0.xy, m0.zw);
    float m2 = min(m1.x, m1.y);
    return min(m2, quad3);
#else
#error "Unknown nearest method."
#endif
}

vec2 sample_nearest_velocity(sampler2D Depth, sampler2D MVs, vec2 UV, vec2 inv_resolution)
{
#if NEAREST_METHOD == NEAREST_METHOD_5TAP_CROSS
    vec2 ShiftUV = UV - 0.5 * inv_resolution;
    vec3 depth_quad0 = textureGather(Depth, ShiftUV).xyz;
    vec2 depth_quad1 = textureGatherOffset(Depth, ShiftUV, ivec2(1)).xz;

    vec3 mvx_quad0 = textureGather(MVs, ShiftUV, 0).xyz;
    vec2 mvx_quad1 = textureGatherOffset(MVs, ShiftUV, ivec2(1), 0).xz;
    vec3 mvy_quad0 = textureGather(MVs, ShiftUV, 1).xyz;
    vec2 mvy_quad1 = textureGatherOffset(MVs, ShiftUV, ivec2(1), 1).xz;

    vec2 mv = vec2(mvx_quad0.x, mvy_quad0.x);
    float d = depth_quad0.x;

    if (depth_quad0.y < d) { mv = vec2(mvx_quad0.y, mvy_quad0.y); d = depth_quad0.y; }
    if (depth_quad0.z < d) { mv = vec2(mvx_quad0.z, mvy_quad0.z); d = depth_quad0.z; }
    if (depth_quad1.x < d) { mv = vec2(mvx_quad1.x, mvy_quad1.x); d = depth_quad1.x; }
    if (depth_quad1.y < d) { mv = vec2(mvx_quad1.y, mvy_quad1.y);                    }
    return mv;
#elif NEAREST_METHOD == NEAREST_METHOD_5TAP_DIAMOND
    float d = textureLod(Depth, UV, 0.0).x;
    float d0 = textureLodOffset(Depth, UV, 0.0, ivec2(-1, -1)).x;
	float d1 = textureLodOffset(Depth, UV, 0.0, ivec2(+1, -1)).x;
	float d2 = textureLodOffset(Depth, UV, 0.0, ivec2(-1, +1)).x;
	float d3 = textureLodOffset(Depth, UV, 0.0, ivec2(+1, +1)).x;

    vec2 mv = textureLod(MVs, UV, 0.0).xy;
    vec2 mv0 = textureLodOffset(MVs, UV, 0.0, ivec2(-1, -1)).xy;
    vec2 mv1 = textureLodOffset(MVs, UV, 0.0, ivec2(+1, -1)).xy;
    vec2 mv2 = textureLodOffset(MVs, UV, 0.0, ivec2(-1, +1)).xy;
    vec2 mv3 = textureLodOffset(MVs, UV, 0.0, ivec2(+1, +1)).xy;

    if (d0 < d) { mv = mv0; d = d0; }
    if (d1 < d) { mv = mv1; d = d1; }
    if (d2 < d) { mv = mv2; d = d2; }
    if (d3 < d) { mv = mv3;         }
    return mv;
#elif NEAREST_METHOD == NEAREST_METHOD_3x3
    vec2 mv = textureLodOffset(MVs, UV, 0.0, ivec2(1)).xy;
    float d = textureLodOffset(Depth, UV, 0.0, ivec2(1)).x;

    vec2 ShiftUV = UV - 0.5 * inv_resolution;
    vec4 quad0 = textureGather(Depth, ShiftUV, 0);
    vec2 quad1 = textureGatherOffset(Depth, ShiftUV, ivec2(1, 0), 0).yz;
    vec2 quad2 = textureGatherOffset(Depth, ShiftUV, ivec2(0, 1), 0).xy;

    vec4 mvx_quad0 = textureGather(MVs, ShiftUV, 0);
    vec2 mvx_quad1 = textureGatherOffset(MVs, ShiftUV, ivec2(1, 0), 0).yz;
    vec2 mvx_quad2 = textureGatherOffset(MVs, ShiftUV, ivec2(0, 1), 0).xy;

    vec4 mvy_quad0 = textureGather(MVs, ShiftUV, 1);
    vec2 mvy_quad1 = textureGatherOffset(MVs, ShiftUV, ivec2(1, 0), 1).yz;
    vec2 mvy_quad2 = textureGatherOffset(MVs, ShiftUV, ivec2(0, 1), 1).xy;

    if (quad0.x < d) { mv = vec2(mvx_quad0.x, mvy_quad0.x); d = quad0.x; }
    if (quad0.y < d) { mv = vec2(mvx_quad0.y, mvy_quad0.y); d = quad0.y; }
    if (quad0.z < d) { mv = vec2(mvx_quad0.z, mvy_quad0.z); d = quad0.z; }
    if (quad0.w < d) { mv = vec2(mvx_quad0.w, mvy_quad0.w); d = quad0.w; }
    if (quad1.x < d) { mv = vec2(mvx_quad1.x, mvy_quad1.x); d = quad1.x; }
    if (quad1.y < d) { mv = vec2(mvx_quad1.y, mvy_quad1.y); d = quad1.y; }
    if (quad2.x < d) { mv = vec2(mvx_quad2.x, mvy_quad2.x); d = quad2.x; }
    if (quad2.y < d) { mv = vec2(mvx_quad2.y, mvy_quad2.y);              }
    return mv;
#endif
}

// From: https://gist.github.com/TheRealMJP/c83b8c0f46b63f3a88a5986f4fa982b1
mediump vec3 sample_catmull_rom(mediump sampler2D tex, vec2 uv, vec4 rt_dimensions)
{
    // We're going to sample a a 4x4 grid of texels surrounding the target UV coordinate. We'll do this by rounding
    // down the sample location to get the exact center of our "starting" texel. The starting texel will be at
    // location [1, 1] in the grid, where [0, 0] is the top left corner.
    vec2 samplePos = uv * rt_dimensions.zw;
    vec2 texPos1 = floor(samplePos - 0.5) + 0.5;

    // Compute the fractional offset from our starting texel to our original sample location, which we'll
    // feed into the Catmull-Rom spline function to get our filter weights.
    vec2 f = samplePos - texPos1;

    // Compute the Catmull-Rom weights using the fractional offset that we calculated earlier.
    // These equations are pre-expanded based on our knowledge of where the texels will be located,
    // which lets us avoid having to evaluate a piece-wise function.
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);

    // Work out weighting factors and sampling offsets that will let us use bilinear filtering to
    // simultaneously evaluate the middle 2 samples from the 4x4 grid.
    vec2 w12 = w1 + w2;
    vec2 offset12 = w2 / (w1 + w2);

    // Compute the final UV coordinates we'll use for sampling the texture
    vec2 texPos0 = texPos1 - 1.0;
    vec2 texPos3 = texPos1 + 2.0;
    vec2 texPos12 = texPos1 + offset12;

    texPos0 *= rt_dimensions.xy;
    texPos3 *= rt_dimensions.xy;
    texPos12 *= rt_dimensions.xy;

    mediump vec3 result = vec3(0.0);
    result += textureLod(tex, texPos0, 0.0).rgb * w0.x * w0.y;
    result += textureLod(tex, vec2(texPos12.x, texPos0.y), 0.0).rgb * w12.x * w0.y;
    result += textureLod(tex, vec2(texPos3.x, texPos0.y), 0.0).rgb * w3.x * w0.y;

    result += textureLod(tex, vec2(texPos0.x, texPos12.y), 0.0).rgb * w0.x * w12.y;
    result += textureLod(tex, texPos12, 0.0).rgb * w12.x * w12.y;
    result += textureLod(tex, vec2(texPos3.x, texPos12.y), 0.0).rgb * w3.x * w12.y;

    result += textureLod(tex, vec2(texPos0.x, texPos3.y), 0.0).rgb * w0.x * w3.y;
    result += textureLod(tex, vec2(texPos12.x, texPos3.y), 0.0).rgb * w12.x * w3.y;
    result += textureLod(tex, texPos3, 0.0).rgb * w3.x * w3.y;

    return result;
}
#endif