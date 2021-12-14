#ifndef PCF_H_
#define PCF_H_

#include "../inc/global_bindings.h"

#ifdef CLUSTERER_BINDLESS
#include "linear_shadow_sampler.h"
#endif

#ifdef SHADOW_MAP_PCF_KERNEL_WIDE
// 6x6 kernel with 3x3 gathers.

// TODO: This seems pretty heavy on ALU with 12 trancendentals ...
// Need some windowing function to avoid annoying artifacts near end of filter support.
mediump vec4 shadow_map_pcf_kernel(mediump vec4 points)
{
	vec4 points2 = points * points;
	return exp2(-0.375 * points2) * (1.0 - points2 / 9.0);
}

mediump vec2 shadow_map_pcf_kernel(mediump vec2 points)
{
	vec2 points2 = points * points;
	return exp2(-0.375 * points2) * (1.0 - points2 / 9.0);
}

mediump float shadow_map_pcf_horiz_sum(mediump vec4 kernel)
{
	mediump vec2 sum2 = kernel.xy + kernel.zw;
	return sum2.x + sum2.y;
}

#define SAMPLE_PCF_ADJUST() \
	vec2 i_coord = clip_uv.xy * resolution - 1.5; \
	vec2 floored_i_coord = floor(i_coord); \
	vec2 f_coord = i_coord - floored_i_coord; \
	clip_uv.xy = floored_i_coord / resolution

#define SAMPLE_PCF_ACCUM(var, gather_samples, kernel) \
	var += dot(gather_samples, kernel); \
	total_w += shadow_map_pcf_horiz_sum(kernel)

#define SAMPLE_PCF_GATHER(var, t) \
{ \
	SAMPLE_PCF_ADJUST(); \
	mediump vec4 c00 = textureGather(t, clip_uv, ref_z); \
	mediump vec4 c10 = textureGatherOffset(t, clip_uv, ref_z, ivec2(2, 0)); \
	mediump vec4 c20 = textureGatherOffset(t, clip_uv, ref_z, ivec2(4, 0)); \
	mediump vec4 c01 = textureGatherOffset(t, clip_uv, ref_z, ivec2(0, 2)); \
	mediump vec4 c11 = textureGatherOffset(t, clip_uv, ref_z, ivec2(2, 2)); \
	mediump vec4 c21 = textureGatherOffset(t, clip_uv, ref_z, ivec2(4, 2)); \
	mediump vec4 c02 = textureGatherOffset(t, clip_uv, ref_z, ivec2(0, 4)); \
	mediump vec4 c12 = textureGatherOffset(t, clip_uv, ref_z, ivec2(2, 4)); \
	mediump vec4 c22 = textureGatherOffset(t, clip_uv, ref_z, ivec2(4, 4)); \
	mediump vec4 horiz0 = f_coord.x + vec4(2.0, 1.0, 0.0, -1.0); \
	mediump vec4 vert0 = f_coord.y + vec4(2.0, 1.0, 0.0, -1.0); \
	mediump vec2 horiz1 = f_coord.x + vec2(-2.0, -3.0); \
	mediump vec2 vert1 = f_coord.y + vec2(-2.0, -3.0); \
	mediump vec4 horiz_coeff0 = shadow_map_pcf_kernel(horiz0); \
	mediump vec4 vert_coeff0 = shadow_map_pcf_kernel(vert0); \
	mediump vec2 horiz_coeff1 = shadow_map_pcf_kernel(horiz1); \
	mediump vec2 vert_coeff1 = shadow_map_pcf_kernel(vert1); \
	var = 0.0; \
	mediump float total_w = 0.0; \
	SAMPLE_PCF_ACCUM(var, c00, horiz_coeff0.xyyx * vert_coeff0.yyxx); \
	SAMPLE_PCF_ACCUM(var, c10, horiz_coeff0.zwwz * vert_coeff0.yyxx); \
	SAMPLE_PCF_ACCUM(var, c20, horiz_coeff1.xyyx * vert_coeff0.yyxx); \
	SAMPLE_PCF_ACCUM(var, c01, horiz_coeff0.xyyx * vert_coeff0.wwzz); \
	SAMPLE_PCF_ACCUM(var, c11, horiz_coeff0.zwwz * vert_coeff0.wwzz); \
	SAMPLE_PCF_ACCUM(var, c21, horiz_coeff1.xyyx * vert_coeff0.wwzz); \
	SAMPLE_PCF_ACCUM(var, c02, horiz_coeff0.xyyx * vert_coeff1.yyxx); \
	SAMPLE_PCF_ACCUM(var, c12, horiz_coeff0.zwwz * vert_coeff1.yyxx); \
	SAMPLE_PCF_ACCUM(var, c22, horiz_coeff1.xyyx * vert_coeff1.yyxx); \
	var /= total_w; \
}

#define SAMPLE_PCF_KERNEL_BINDLESS(var, tex, index, uv) \
{ \
	vec2 clip_uv = (uv).xy / (uv).w; \
    float ref_z = (uv).z / (uv).w; \
    vec2 resolution = vec2(textureSize(tex[nonuniformEXT(index)], 0).xy); \
	SAMPLE_PCF_GATHER(var, nonuniformEXT(sampler2DShadow(tex[index], LinearShadowSampler))); \
}

#define SAMPLE_PCF_KERNEL(var, tex, uv) \
{ \
	vec2 clip_uv = (uv).xy / (uv).w; \
	float ref_z = (uv).z / (uv).w; \
	vec2 resolution = vec2(textureSize(tex, 0)); \
	SAMPLE_PCF_GATHER(var, tex); \
}

#define SAMPLE_PCF_KERNEL_LAYER_NOPROJ(var, tex, uv, layer) \
{ \
	vec3 clip_uv = vec3((uv).xy, layer); \
	float ref_z = (uv).z; \
	vec2 resolution = vec2(textureSize(tex, 0).xy); \
	SAMPLE_PCF_GATHER(var, tex); \
}
#else
#define SAMPLE_PCF_KERNEL_BINDLESS(var, tex, index, uv) \
	var = textureProjLod(nonuniformEXT(sampler2DShadow(tex[index], LinearShadowSampler)), uv, 0.0)
#define SAMPLE_PCF_KERNEL(var, tex, uv) \
	var = textureProjLod(tex, uv, 0.0)
#define SAMPLE_PCF_KERNEL_LAYER_NOPROJ(var, tex, uv, layer) \
	var = texture(tex, vec4((uv).xy, layer, (uv).z))
#endif

#endif
