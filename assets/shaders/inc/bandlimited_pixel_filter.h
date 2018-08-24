#ifndef BANDLIMITED_PIXEL_FILTER_H_
#define BANDLIMITED_PIXEL_FILTER_H_

struct BandlimitedPixelInfo
{
	vec2 uv0;
	vec2 uv1;
	vec2 uv2;
	vec2 uv3;
	mediump vec4 weights;
	mediump float l;
};

// The cosine filter convolved with rect has a support of 1.5 pixels.
// We can sample 4x4 regions, so we can deal with 2.0 pixel range in our filter.
const float maximum_support_extent = 2.0 / 1.5;

// Our Taylor approximation is not exact, normalize so the peak is 1.
const float taylor_pi = 1.00452485553;
const float taylor_normalization = 1.0 / taylor_pi;

const float PI = 3.14159265359;
const float PI_half = 0.5 * PI;

// p must be in range [-pi, pi].
#define gen_taylor(T) \
mediump T taylor_sin(mediump T p) \
{ \
	mediump T p2 = p * p; \
	mediump T p3 = p * p2; \
	mediump T p5 = p2 * p3; \
	return taylor_normalization * (p - p3 * (1.0 / 6.0) + p5 * (1.0 / 120.0)); \
}
gen_taylor(float)
gen_taylor(vec2)
gen_taylor(vec3)
gen_taylor(vec4)

// Given weights, compute a bilinear filter which implement the weight.
// All weights are known to be non-negative.
mediump vec3 compute_uv_phase_weight(mediump vec2 weights_u, mediump vec2 weights_v)
{
	mediump float w = dot(weights_u.xyxy, weights_v.xxyy);
	mediump float x = weights_u.y / max(weights_u.x + weights_u.y, 0.001);
	mediump float y = weights_v.y / max(weights_v.x + weights_v.y, 0.001);
	return vec3(x, y, w);
}

BandlimitedPixelInfo compute_pixel_weights(vec2 uv, vec2 size, vec2 inv_size)
{
	// Get derivatives in texel space.
	vec2 extent = max(fwidth(uv) * size, 1.0 / 256.0);
	mediump vec2 inv_extent = 1.0 / extent;

	// Get base pixel and phase, range [0, 1).
	vec2 pixel = uv * size - 0.5;
	vec2 base_pixel = floor(pixel);
	vec2 phase = pixel - base_pixel;

	BandlimitedPixelInfo info;
	if (any(greaterThanEqual(extent, vec2(maximum_support_extent))))
	{
		// We need to just do regular minimization filtering.
		info = BandlimitedPixelInfo(vec2(0.0), vec2(0.0), vec2(0.0), vec2(0.0),
		                            vec4(0.0, 0.0, 0.0, 0.0), 0.0);
	}
	else if (all(lessThanEqual(extent, vec2(0.5))))
	{
		// We can resolve the filter by just sampling a single 2x2 block.
		mediump vec2 shift = 0.5 + 0.5 * taylor_sin(PI_half * clamp(inv_extent * (phase - 0.5), -1.0, 1.0));
		info = BandlimitedPixelInfo((base_pixel + 0.5 + shift) * inv_size, vec2(0.0), vec2(0.0), vec2(0.0),
		                            vec4(1.0, 0.0, 0.0, 0.0), 1.0);
	}
	else
	{
		// Full 4x4 sampling.

		// Fade between bandlimited and normal sampling.
		mediump float max_extent = max(extent.x, extent.y);
		mediump float l = clamp(1.0 - (max_extent - 1.0) / (maximum_support_extent - 1.0), 0.0, 1.0);

		mediump vec4 sine_phases_x = PI_half * clamp(inv_extent.x * (phase.x + vec4(1.5, 0.5, -0.5, -1.5)), -1.0, 1.0);
		mediump vec4 sines_x = taylor_sin(sine_phases_x);

		mediump vec4 sine_phases_y = PI_half * clamp(inv_extent.y * (phase.y + vec4(1.5, 0.5, -0.5, -1.5)), -1.0, 1.0);
		mediump vec4 sines_y = taylor_sin(sine_phases_y);

		mediump vec2 sine_phases_end = PI_half * clamp(inv_extent * (phase - 2.5), -1.0, 1.0);
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
									vec4(w0.z, w1.z, w2.z, w3.z), l);
	}

	return info;
}

mediump vec4 sample_bandlimited_pixel(sampler2D samp, vec2 uv, BandlimitedPixelInfo info, float lod)
{
	mediump vec4 color = texture(samp, uv);
	if (info.l > 0.0)
	{
		mediump vec4 bandlimited = info.weights.x * textureLod(samp, info.uv0, lod);
		if (info.weights.x < 1.0)
		{
			bandlimited += info.weights.y * textureLod(samp, info.uv1, lod);
			bandlimited += info.weights.z * textureLod(samp, info.uv2, lod);
			bandlimited += info.weights.w * textureLod(samp, info.uv3, lod);
		}
		color = mix(color, bandlimited, info.l);
	}
	return color;
}

#endif