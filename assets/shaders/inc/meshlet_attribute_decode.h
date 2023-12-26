#ifndef MESHLET_ATTRIBUTE_DECODE_H_
#define MESHLET_ATTRIBUTE_DECODE_H_

vec3 attribute_decode_snorm_exp_position(i16vec3 payload, int exponent)
{
    vec3 fp_pos = ldexp(vec3(payload), ivec3(exponent));
    return fp_pos;
}

vec2 attribute_decode_snorm_exp_uv(i16vec2 payload, int exponent)
{
    return 0.5 * ldexp(vec2(payload), ivec2(exponent)) + 0.5;
}

mediump vec3 attribute_decode_oct_normal(mediump vec2 f)
{
	mediump vec3 n = vec3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
	mediump float t = max(-n.z, 0.0);
	n.xy += mix(vec2(t), vec2(-t), greaterThanEqual(n.xy, vec2(0.0)));
	return normalize(n);
}

// Adapted from: https://knarkowicz.wordpress.com/2014/04/16/octahedron-normal-vector-encoding/
// https://twitter.com/Stubbesaurus/status/9379947905532272640
mediump mat2x4 attribute_decode_oct8_normal_tangent(u8vec4 payload, bool t_sign)
{
	mediump vec4 f = vec4(i8vec4(payload)) / 127.0;
    mediump vec3 N = attribute_decode_oct_normal(f.xy);
    mediump vec3 T = attribute_decode_oct_normal(f.zw);
    return mat2x4(vec4(N, 0.0), vec4(T, t_sign ? -1.0 : 1.0));
}

#endif
