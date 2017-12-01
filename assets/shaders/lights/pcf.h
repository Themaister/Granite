#ifndef PCF_H_
#define PCF_H_

#define PCF_3x3

#ifdef PCF_3x3
#define SAMPLE_PCF(tex, uv, x, y) textureLodOffset(tex, uv, 0.0, ivec2(x, y))
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
#else
#define SAMPLE_PCF_KERNEL(var, tex, uv) var = textureProjLod(tex, uv, 0.0)
#endif

#endif