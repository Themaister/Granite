#ifndef SRGB_H_
#define SRGB_H_

mediump vec3 decode_srgb(mediump vec3 c)
{
	bvec3 small = lessThanEqual(c, vec3(0.0404482362771082));
	mediump vec3 small_side = c / 12.92;
	mediump vec3 pow_side = pow(((c + 0.055) / 1.055), vec3(2.4));
	return clamp(mix(pow_side, small_side, small), vec3(0.0), vec3(1.0));
}

mediump vec3 encode_srgb(mediump vec3 c)
{
	bvec3 small = lessThanEqual(c, vec3(0.0031308));
	mediump vec3 small_side = c * 12.92;
	mediump vec3 pow_side = 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055;
	return clamp(mix(pow_side, small_side, small), vec3(0.0), vec3(1.0));
}

#endif
