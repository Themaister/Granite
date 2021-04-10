#ifndef VOLUMETRIC_PROBE_HASH_H_
#define VOLUMETRIC_PROBE_HASH_H_

uvec2 volumetric_probe_hash(ivec3 coord, uint range)
{
	// From: https://www.shadertoy.com/view/XlXcW4 with slight modifications.
	const uint NOISE_PRIME = 1103515245u;
	uvec3 seed = uvec3(coord);
	seed = ((seed >> 8u) ^ seed.yzx) * NOISE_PRIME;
	seed = ((seed >> 8u) ^ seed.yzx) * NOISE_PRIME;
	seed = ((seed >> 8u) ^ seed.yzx) * NOISE_PRIME;
	return (seed.xy >> 16u) & (range - 1u);
}

#endif