#ifndef BANDLIMITED_PIXEL_FILTER_H_
#define BANDLIMITED_PIXEL_FILTER_H_

// In the fast mode, we get maximum 2 texture samples.
// This mode removes the 4x4 sampling case using 4 bilinear samples, and instead just triggers when LOD reaches -1.
// The non-fast mode achieves better filtering around LOD 0, it also has less aliasing for small minimization, i.e.
// LOD around 0.1 and 0.2.
// The non-fast mode has a fairly expensive case around the 4x4 sampling, so unless deemed necessary, fast mode should be used.

#ifndef BANDLIMITED_PIXEL_FAST_MODE
#define BANDLIMITED_PIXEL_FAST_MODE 1
#endif

//#define BANDLIMITED_PIXEL_DEBUG

#if defined(BANDLIMITED_PIXEL_FAST_MODE) && !BANDLIMITED_PIXEL_FAST_MODE
#undef BANDLIMITED_PIXEL_FAST_MODE
#endif

#if defined(BANDLIMITED_PIXEL_DEBUG) && !BANDLIMITED_PIXEL_DEBUG
#undef BANDLIMITED_PIXEL_DEBUG
#endif

// Use sin(x) instead of a Taylor approximation.
// Will be faster if the GPU is half-decent at transcendentals.
#ifndef BANDLIMITED_PIXEL_USE_TRANSCENDENTAL
#define BANDLIMITED_PIXEL_USE_TRANSCENDENTAL 1
#endif

struct BandlimitedPixelInfo
{
	vec2 uv0;
#ifndef BANDLIMITED_PIXEL_FAST_MODE
	vec2 uv1;
	vec2 uv2;
	vec2 uv3;
	mediump vec4 weights;
#endif
	mediump float l;
#ifdef BANDLIMITED_PIXEL_DEBUG
	mediump vec4 debug_tint;
#endif
};

// The cosine filter convolved with rect has a support of 0.5 + d pixels.
// We can sample 4x4 regions, so we can deal with 2.0 pixel range in our filter,
// and the maximum extent value we can have is 1.5.
const float maximum_support_extent = 1.5;

// Our Taylor approximation is not exact, normalize so the peak is 1.
const float taylor_pi_half = 1.00452485553;
const float taylor_normalization = 1.0 / taylor_pi_half;
const float bandlimited_PI = 3.14159265359;
const float bandlimited_PI_half = 0.5 * bandlimited_PI;

#if BANDLIMITED_PIXEL_USE_TRANSCENDENTAL
#define taylor_sin(x) sin(x)
#else
// p must be in range [-pi, pi].
#define gen_taylor(T) \
mediump T taylor_sin(mediump T p) \
{ \
	mediump T p2 = p * p; \
	mediump T p3 = p * p2; \
	mediump T p5 = p2 * p3; \
	return clamp(taylor_normalization * (p - p3 * (1.0 / 6.0) + p5 * (1.0 / 120.0)), -1.0, 1.0); \
}

// No templates in GLSL. Stamp out macros.
gen_taylor(float)
gen_taylor(vec2)
gen_taylor(vec3)
gen_taylor(vec4)
#endif

// Given weights, compute a bilinear filter which implements the weight.
// All weights are known to be non-negative, and separable.
mediump vec3 compute_uv_phase_weight(mediump vec2 weights_u, mediump vec2 weights_v)
{
	// The sum of a bilinear sample has combined weight of 1, we will need to adjust the resulting sample
	// to match our actual weight sum.
	mediump float w = dot(weights_u.xyxy, weights_v.xxyy);
	mediump float x = weights_u.y / max(weights_u.x + weights_u.y, 0.001);
	mediump float y = weights_v.y / max(weights_v.x + weights_v.y, 0.001);
	return vec3(x, y, w);
}

// Smaller value for extent_mod sharpens (more aliasing),
// larger value blurs more (more blurry).
BandlimitedPixelInfo compute_pixel_weights(vec2 uv, vec2 size, vec2 inv_size, mediump float extent_mod)
{
	// Get derivatives in texel space.
	// Need a non-zero derivative.
	vec2 extent = max(fwidth(uv) * size * extent_mod, 1.0 / 256.0);

	// Get base pixel and phase, range [0, 1).
	vec2 pixel = uv * size - 0.5;
	vec2 base_pixel = floor(pixel);
	vec2 phase = pixel - base_pixel;

	BandlimitedPixelInfo info;
#ifdef BANDLIMITED_PIXEL_FAST_MODE
	if (any(greaterThan(extent, vec2(1.0))))
	{
		// We need to just do regular minimization filtering.
		info = BandlimitedPixelInfo(vec2(0.0), 0.0
#ifdef BANDLIMITED_PIXEL_DEBUG
			, vec4(1.0, 0.5, 0.5, 1.0)
#endif
		);
	}
	else
	{
		// We can resolve the filter by just sampling a single 2x2 block.
		// Lerp between normal sampling at LOD 0, and bandlimited pixel filter at LOD -1.
		mediump vec2 shift = 0.5 + 0.5 * taylor_sin(bandlimited_PI_half * clamp((phase - 0.5) / min(extent, vec2(0.5)), -1.0, 1.0));
		mediump float max_extent = max(extent.x, extent.y);
		mediump float l = clamp(2.0 - 2.0 * max_extent, 0.0, 1.0); // max_extent = 1 -> l = 0, max_extent = 0.5 -> l = 1.
		info = BandlimitedPixelInfo((base_pixel + 0.5 + shift) * inv_size, l
#ifdef BANDLIMITED_PIXEL_DEBUG
			, vec4(0.5, 0.5, 1.0, 1.0)
#endif
		);
	}
#else
	mediump vec2 inv_extent = 1.0 / extent;
	if (any(greaterThan(extent, vec2(maximum_support_extent))))
	{
		// We need to just do regular minimization filtering.
		info = BandlimitedPixelInfo(vec2(0.0), vec2(0.0), vec2(0.0), vec2(0.0),
		                            vec4(0.0, 0.0, 0.0, 0.0), 0.0
#ifdef BANDLIMITED_PIXEL_DEBUG
			, vec4(1.0, 0.5, 0.5, 1.0)
#endif
		);
	}
	else if (all(lessThanEqual(extent, vec2(0.5))))
	{
		// We can resolve the filter by just sampling a single 2x2 block.
		mediump vec2 shift = 0.5 + 0.5 * taylor_sin(bandlimited_PI_half * clamp(inv_extent * (phase - 0.5), -1.0, 1.0));
		info = BandlimitedPixelInfo((base_pixel + 0.5 + shift) * inv_size, vec2(0.0), vec2(0.0), vec2(0.0),
		                            vec4(1.0, 0.0, 0.0, 0.0), 1.0
#ifdef BANDLIMITED_PIXEL_DEBUG
				, vec4(0.5, 1.0, 0.5, 1.0)
#endif
		);
	}
	else
	{
		// Full 4x4 sampling.

		// Fade between bandlimited and normal sampling.
		// Fully use bandlimited filter at LOD 0, normal filtering at approx. LOD -0.5.
		mediump float max_extent = max(extent.x, extent.y);
		mediump float l = clamp(1.0 - (max_extent - 1.0) / (maximum_support_extent - 1.0), 0.0, 1.0);

		mediump vec4 sine_phases_x = bandlimited_PI_half * clamp(inv_extent.x * (phase.x + vec4(1.5, 0.5, -0.5, -1.5)), -1.0, 1.0);
		mediump vec4 sines_x = taylor_sin(sine_phases_x);

		mediump vec4 sine_phases_y = bandlimited_PI_half * clamp(inv_extent.y * (phase.y + vec4(1.5, 0.5, -0.5, -1.5)), -1.0, 1.0);
		mediump vec4 sines_y = taylor_sin(sine_phases_y);

		mediump vec2 sine_phases_end = bandlimited_PI_half * clamp(inv_extent * (phase - 2.5), -1.0, 1.0);
		mediump vec2 sines_end = taylor_sin(sine_phases_end);

		mediump vec4 weights_x = 0.5 * (sines_x - vec4(sines_x.yzw, sines_end.x));
		mediump vec4 weights_y = 0.5 * (sines_y - vec4(sines_y.yzw, sines_end.y));

		mediump vec3 w0 = compute_uv_phase_weight(weights_x.xy, weights_y.xy);
		mediump vec3 w1 = compute_uv_phase_weight(weights_x.zw, weights_y.xy);
		mediump vec3 w2 = compute_uv_phase_weight(weights_x.xy, weights_y.zw);
		mediump vec3 w3 = compute_uv_phase_weight(weights_x.zw, weights_y.zw);

		info = BandlimitedPixelInfo((base_pixel - 0.5 + w0.xy) * inv_size,
									(base_pixel + vec2(1.5, -0.5) + w1.xy) * inv_size,
									(base_pixel + vec2(-0.5, 1.5) + w2.xy) * inv_size,
									(base_pixel + 1.5 + w3.xy) * inv_size,
									vec4(w0.z, w1.z, w2.z, w3.z), l
#ifdef BANDLIMITED_PIXEL_DEBUG
				, vec4(0.5, 0.5, 1.0, 1.0)
#endif
		);
	}
#endif

	return info;
}

mediump vec4 sample_bandlimited_pixel(sampler2D samp, vec2 uv, BandlimitedPixelInfo info, mediump float lod_bias)
{
	mediump vec4 color = texture(samp, uv, lod_bias);
	if (info.l > 0.0)
	{
#ifndef BANDLIMITED_PIXEL_FAST_MODE
		mediump vec4 bandlimited = info.weights.x * textureLod(samp, info.uv0, 0.0);
		if (info.weights.x < 1.0)
		{
			bandlimited += info.weights.y * textureLod(samp, info.uv1, 0.0);
			bandlimited += info.weights.z * textureLod(samp, info.uv2, 0.0);
			bandlimited += info.weights.w * textureLod(samp, info.uv3, 0.0);
		}
		color = mix(color, bandlimited, info.l);
#else
		mediump vec4 bandlimited = textureLod(samp, info.uv0, 0.0);
		color = mix(color, bandlimited, info.l);
#endif
	}
#ifdef BANDLIMITED_PIXEL_DEBUG
	color *= info.debug_tint;
#endif
	return color;
}

mediump vec4 sample_bandlimited_pixel_array(sampler2DArray samp, vec3 uv, BandlimitedPixelInfo info, mediump float lod_bias)
{
	mediump vec4 color = texture(samp, uv, lod_bias);
	if (info.l > 0.0)
	{
#ifndef BANDLIMITED_PIXEL_FAST_MODE
		mediump vec4 bandlimited = info.weights.x * textureLod(samp, vec3(info.uv0, uv.z), 0.0);
		if (info.weights.x < 1.0)
		{
			bandlimited += info.weights.y * textureLod(samp, vec3(info.uv1, uv.z), 0.0);
			bandlimited += info.weights.z * textureLod(samp, vec3(info.uv2, uv.z), 0.0);
			bandlimited += info.weights.w * textureLod(samp, vec3(info.uv3, uv.z), 0.0);
		}
		color = mix(color, bandlimited, info.l);
#else
		mediump vec4 bandlimited = textureLod(samp, vec3(info.uv0, uv.z), 0.0);
		color = mix(color, bandlimited, info.l);
#endif
	}
#ifdef BANDLIMITED_PIXEL_DEBUG
	color *= info.debug_tint;
#endif
	return color;
}

#endif
