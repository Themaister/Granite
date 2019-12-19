#ifndef PCF_H_
#define PCF_H_

#ifndef SHADOW_MAP_PCF_KERNEL_WIDTH
#define SHADOW_MAP_PCF_KERNEL_WIDTH 1
#endif

#define SAMPLE_PCF_BINDLESS(tex, index, uv, x, y) \
	textureLodOffset(sampler2DShadow(tex[nonuniformEXT(index)], LinearShadowClamp), uv, 0.0, ivec2(x, y))
#define SAMPLE_PCF(tex, uv, x, y) \
	textureLodOffset(tex, uv, 0.0, ivec2(x, y))

#if SHADOW_MAP_PCF_KERNEL_WIDTH == 5
#define SAMPLE_PCF_KERNEL_BINDLESS(var, tex, index, uv) \
{ \
	vec3 clip_uv = (uv).xyz / (uv).w; \
	const mediump float weight = 0.1177491; \
	const mediump float w00 = weight * 1.000000; \
	const mediump float w01 = weight * 0.707106; \
	const mediump float w02 = weight * 0.250000; \
	const mediump float w11 = weight * 0.5; \
	const mediump float w12 = weight * 0.176776; \
	const mediump float w22 = weight * 0.0625; \
	var = w22 * SAMPLE_PCF_BINDLESS(tex, index, clip_uv, -2, -2); \
	var += w12 * SAMPLE_PCF_BINDLESS(tex, index, clip_uv, -1, -2); \
	var += w02 * SAMPLE_PCF_BINDLESS(tex, index, clip_uv, +0, -2); \
	var += w12 * SAMPLE_PCF_BINDLESS(tex, index, clip_uv, +1, -2); \
	var += w22 * SAMPLE_PCF_BINDLESS(tex, index, clip_uv, +2, -2); \
	var += w12 * SAMPLE_PCF_BINDLESS(tex, index, clip_uv, -2, -1); \
	var += w11 * SAMPLE_PCF_BINDLESS(tex, index, clip_uv, -1, -1); \
	var += w01 * SAMPLE_PCF_BINDLESS(tex, index, clip_uv, +0, -1); \
	var += w11 * SAMPLE_PCF_BINDLESS(tex, index, clip_uv, +1, -1); \
	var += w12 * SAMPLE_PCF_BINDLESS(tex, index, clip_uv, +2, -1); \
	var += w02 * SAMPLE_PCF_BINDLESS(tex, index, clip_uv, -2, +0); \
	var += w01 * SAMPLE_PCF_BINDLESS(tex, index, clip_uv, -1, +0); \
	var += w00 * SAMPLE_PCF_BINDLESS(tex, index, clip_uv, +0, +0); \
	var += w01 * SAMPLE_PCF_BINDLESS(tex, index, clip_uv, +1, +0); \
	var += w02 * SAMPLE_PCF_BINDLESS(tex, index, clip_uv, +2, +0); \
	var += w12 * SAMPLE_PCF_BINDLESS(tex, index, clip_uv, -2, +1); \
	var += w11 * SAMPLE_PCF_BINDLESS(tex, index, clip_uv, -1, +1); \
	var += w01 * SAMPLE_PCF_BINDLESS(tex, index, clip_uv, +0, +1); \
	var += w11 * SAMPLE_PCF_BINDLESS(tex, index, clip_uv, +1, +1); \
	var += w12 * SAMPLE_PCF_BINDLESS(tex, index, clip_uv, +2, +1); \
	var += w22 * SAMPLE_PCF_BINDLESS(tex, index, clip_uv, -2, +2); \
	var += w12 * SAMPLE_PCF_BINDLESS(tex, index, clip_uv, -1, +2); \
	var += w02 * SAMPLE_PCF_BINDLESS(tex, index, clip_uv, +0, +2); \
	var += w12 * SAMPLE_PCF_BINDLESS(tex, index, clip_uv, +1, +2); \
	var += w22 * SAMPLE_PCF_BINDLESS(tex, index, clip_uv, +2, +2); \
}

#define SAMPLE_PCF_KERNEL(var, tex, uv) \
{ \
	vec3 clip_uv = (uv).xyz / (uv).w; \
	const mediump float weight = 0.1177491; \
	const mediump float w00 = weight * 1.000000; \
	const mediump float w01 = weight * 0.707106; \
	const mediump float w02 = weight * 0.250000; \
	const mediump float w11 = weight * 0.5; \
	const mediump float w12 = weight * 0.176776; \
	const mediump float w22 = weight * 0.0625; \
	var = w22 * SAMPLE_PCF(tex, clip_uv, -2, -2); \
	var += w12 * SAMPLE_PCF(tex, clip_uv, -1, -2); \
	var += w02 * SAMPLE_PCF(tex, clip_uv, +0, -2); \
	var += w12 * SAMPLE_PCF(tex, clip_uv, +1, -2); \
	var += w22 * SAMPLE_PCF(tex, clip_uv, +2, -2); \
	var += w12 * SAMPLE_PCF(tex, clip_uv, -2, -1); \
	var += w11 * SAMPLE_PCF(tex, clip_uv, -1, -1); \
	var += w01 * SAMPLE_PCF(tex, clip_uv, +0, -1); \
	var += w11 * SAMPLE_PCF(tex, clip_uv, +1, -1); \
	var += w12 * SAMPLE_PCF(tex, clip_uv, +2, -1); \
	var += w02 * SAMPLE_PCF(tex, clip_uv, -2, +0); \
	var += w01 * SAMPLE_PCF(tex, clip_uv, -1, +0); \
	var += w00 * SAMPLE_PCF(tex, clip_uv, +0, +0); \
	var += w01 * SAMPLE_PCF(tex, clip_uv, +1, +0); \
	var += w02 * SAMPLE_PCF(tex, clip_uv, +2, +0); \
	var += w12 * SAMPLE_PCF(tex, clip_uv, -2, +1); \
	var += w11 * SAMPLE_PCF(tex, clip_uv, -1, +1); \
	var += w01 * SAMPLE_PCF(tex, clip_uv, +0, +1); \
	var += w11 * SAMPLE_PCF(tex, clip_uv, +1, +1); \
	var += w12 * SAMPLE_PCF(tex, clip_uv, +2, +1); \
	var += w22 * SAMPLE_PCF(tex, clip_uv, -2, +2); \
	var += w12 * SAMPLE_PCF(tex, clip_uv, -1, +2); \
	var += w02 * SAMPLE_PCF(tex, clip_uv, +0, +2); \
	var += w12 * SAMPLE_PCF(tex, clip_uv, +1, +2); \
	var += w22 * SAMPLE_PCF(tex, clip_uv, +2, +2); \
}
#elif SHADOW_MAP_PCF_KERNEL_WIDTH == 3
#define SAMPLE_PCF_KERNEL_BINDLESS(var, tex, index, uv) \
{ \
	vec3 clip_uv = (uv).xyz / (uv).w; \
	var = 0.0625 * SAMPLE_PCF_BINDLESS(tex, index, clip_uv, -1, -1); \
	var += 0.1250 * SAMPLE_PCF_BINDLESS(tex, index, clip_uv, +0, -1); \
	var += 0.0625 * SAMPLE_PCF_BINDLESS(tex, index, clip_uv, +1, -1); \
	var += 0.1250 * SAMPLE_PCF_BINDLESS(tex, index, clip_uv, -1, +0); \
	var += 0.2500 * SAMPLE_PCF_BINDLESS(tex, index, clip_uv, +0, +0); \
	var += 0.1250 * SAMPLE_PCF_BINDLESS(tex, index, clip_uv, +1, +0); \
	var += 0.0625 * SAMPLE_PCF_BINDLESS(tex, index, clip_uv, -1, +1); \
	var += 0.1250 * SAMPLE_PCF_BINDLESS(tex, index, clip_uv, +0, +1); \
	var += 0.0625 * SAMPLE_PCF_BINDLESS(tex, index, clip_uv, +1, +1); \
}

#define SAMPLE_PCF_KERNEL(var, tex, uv) \
{ \
	vec3 clip_uv = (uv).xyz / (uv).w; \
	var = 0.0625 * SAMPLE_PCF(tex, clip_uv, -1, -1); \
	var += 0.1250 * SAMPLE_PCF(tex, clip_uv, +0, -1); \
	var += 0.0625 * SAMPLE_PCF(tex, clip_uv, +1, -1); \
	var += 0.1250 * SAMPLE_PCF(tex, clip_uv, -1, +0); \
	var += 0.2500 * SAMPLE_PCF(tex, clip_uv, +0, +0); \
	var += 0.1250 * SAMPLE_PCF(tex, clip_uv, +1, +0); \
	var += 0.0625 * SAMPLE_PCF(tex, clip_uv, -1, +1); \
	var += 0.1250 * SAMPLE_PCF(tex, clip_uv, +0, +1); \
	var += 0.0625 * SAMPLE_PCF(tex, clip_uv, +1, +1); \
}
#elif SHADOW_MAP_PCF_KERNEL_WIDTH == 1
#define SAMPLE_PCF_KERNEL_BINDLESS(var, tex, index, uv) var = textureProjLod(sampler2DShadow(tex[nonuniformEXT(index)], LinearShadowClamp), uv, 0.0)
#define SAMPLE_PCF_KERNEL(var, tex, uv) var = textureProjLod(tex, uv, 0.0)
#else
#error "Unsupported PCF kernel width."
#endif

#endif
