#ifndef PROJECT_DIRECTION_H_
#define PROJECT_DIRECTION_H_

vec3 project_direction_to_clip_space(vec3 clip, vec3 world_direction, mat4 view_projection)
{
	// For screen-space tracing.
	// Given a world position P and a direction vector D,
	// we want to construct a direction in clip space.
	// clip is in [-1, +1] for XY and [0, 1] for Z.
	// Return value is in same range. For tracing in NDC space, scale XY by 0.5 afterwards.

	// The naive implementation is something like:
	// vec4 clip_P = view_projection * vec4(P, 1.0);
	// vec4 clip_PD = view_projection * vec4(P + D, 1.0);
	// vec3 clip_pos_start = clip_P.xyz / clip_P.w;
	// vec3 clip_pos_end = clip_PD.xyz / clip_PD.w;
	// vec3 diff_clip_pos = clip_pos_end - clip_pos_start;

	// We are not interested in the exact difference, but we want a direction vector
	// that can be marched through in screen-space. It is not immediately obvious that there
	// is a unique direction in clip space.

	// First, rewrite D in terms of a scaling factor k, and split the sums.
	// clip_D = view_projection * vec4(k * D, 0.0);
	// clip_PD = view_projection * vec4(P, 1.0) + clip_D;
	// C_p = clip_P.xyz / clip_P.w;
	// C_pd = clip_P.xyz + k * clip_D.xyz / (clip_P.w + k * clip_D.w);
	// If we look at the difference C_pd - C_p, we can prove that the result has a unique normalized result.
	// C_pd - C_p = clip_P.xyz + k * clip_D.xyz / (clip_P.w + k * clip_D.w) - clip_P.xyz / clip_P.w;
	//            = [(clip_P.xyz + k * clip_D.xyz) * clip_P.w - clip_P.xyz * (clip_P.w + k * clip_D.w)] /
	//                  (clip_P.w * (clip_P.w + k * clip_D.w))

	// The denominator is a scalar here and we can guarantee that it is positive.
	// When we trace in screen-space, P guarantees W > 0,
	// and we have no need to trace beyond the W = 0 plane either way (if tracing towards camera),
	// we will hit near plane (Z = 0) before that happens.
	// For purposes of normalization, we can ignore the nominator.

	// (clip_P.xyz + k * clip_D.xyz) * clip_P.w - clip_P.xyz * (clip_P.w + k * clip_D.w) =
	// clip_P.xyz * clip_P.w + k * clip_D.xyz * clip_P.w - clip_P.xyz * clip_P.w - k * clip_D.w * clip_P.xyz =
	// k * clip_D.xyz * clip_P.w - k * clip_D.w * clip_P.xyz =
	// k * [clip_D.xyz * clip_P.w - clip_D.w * clip_P.xyz]

	// This proves the normalized vector is unique for any k (assuming that we never cross W = 0 plane).
	// To further simplify, we can use the projected clip coordinates from depth-buffer directly.
	// k * [clip_D.xyz * clip_P.w - clip_D.w * clip_P.xyz] =
	// k * clip_P.w * [clip_D.xyz - clip_D.w * project(clip_P)]
	// normalize(k * clip_P.w * [clip_D.xyz - clip_D.w * project(clip_P)]) =
	// normalize(clip_D.xyz - clip_D.w * project(clip_P))
	vec4 clip_D = view_projection * vec4(world_direction, 0.0);
	return normalize(clip_D.xyz - clip_D.w * clip);
}

#endif