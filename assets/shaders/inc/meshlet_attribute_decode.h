#ifndef MESHLET_ATTRIBUTE_DECODE_H_
#define MESHLET_ATTRIBUTE_DECODE_H_

vec3 attribute_decode_snorm_exp_position(uvec2 payload)
{
	ivec3 sint_value = ivec3(
		bitfieldExtract(int(payload.x), 0, 16),
		bitfieldExtract(int(payload.x), 16, 16),
		bitfieldExtract(int(payload.y), 0, 16));
	int exp = bitfieldExtract(int(payload.y), 16, 16);
	return vec3(
		ldexp(float(sint_value.x), exp),
		ldexp(float(sint_value.y), exp),
		ldexp(float(sint_value.z), exp));
}

vec2 attribute_decode_snorm_exp_uv(uvec2 payload)
{
	ivec2 sint_value = ivec2(
		bitfieldExtract(int(payload.x), 0, 16),
		bitfieldExtract(int(payload.x), 16, 16));
	int exp = bitfieldExtract(int(payload.y), 0, 16);
	return 0.5 * vec2(
		ldexp(float(sint_value.x), exp),
		ldexp(float(sint_value.y), exp)) + 0.5;
}

// Adapted from: https://knarkowicz.wordpress.com/2014/04/16/octahedron-normal-vector-encoding/
// https://twitter.com/Stubbesaurus/status/9379947905532272640
mediump vec4 attribute_decode_oct8_normal_tangent(uint payload)
{
	mediump vec4 f = unpackSnorm4x8(payload);
	mediump vec3 n = vec3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
	mediump float t = max(-n.z, 0.0);
	n.xy += mix(vec2(t), vec2(-t), greaterThanEqual(n.xy, vec2(0.0)));
	return vec4(normalize(n), f.w != 0.0 ? -1.0 : 1.0);
}

#endif