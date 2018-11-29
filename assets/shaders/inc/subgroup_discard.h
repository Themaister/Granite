#ifndef SUBGROUP_DISCARD_H_
#define SUBGROUP_DISCARD_H_

#ifdef SUBGROUP_BASIC
#extension GL_KHR_shader_subgroup_basic : require
#endif

#ifdef SUBGROUP_VOTE
#extension GL_KHR_shader_subgroup_vote : require
#endif

#ifdef SUBGROUP_CLUSTERED
#extension GL_KHR_shader_subgroup_clustered : require
#endif

#ifdef SUBGROUP_BALLOT
#extension GL_KHR_shader_subgroup_ballot : require
#endif

#ifdef SUBGROUP_QUAD
#extension GL_KHR_shader_subgroup_quad : require
#endif

void quad_discard(bool to_discard)
{
#if defined(SUBGROUP_CLUSTERED)
	// This is the cleanest one.
	// Invocations in a quad must align to a cluster of 4.
	if (subgroupClusteredAnd(int(to_discard), 4) != 0)
		discard;
#elif defined(SUBGROUP_QUAD)
	// Next best solution. Broadcast all lanes in the quad and decide.
	bvec4 lanes = bvec4(
			subgroupQuadBroadcast(to_discard, 0),
			subgroupQuadBroadcast(to_discard, 1),
			subgroupQuadBroadcast(to_discard, 2),
			subgroupQuadBroadcast(to_discard, 3));
	if (all(lanes))
		discard;
#elif defined(SUBGROUP_BALLOT)
	// A bit more awkward.
	uvec4 discard_mask = subgroupBallot(to_discard);
	uint ballot_shift = gl_SubgroupInvocationID & (7u << 2u);
	uvec4 ballot_mask = mix(uvec4(0u), uvec4(0xfu << ballot_shift), equal(uvec4(gl_SubgroupInvocationID >> 5u), uvec4(0u, 1u, 2u, 3u)));
	if (all(equal(ballot_mask & discard_mask, ballot_mask)))
		discard;
#elif defined(SUBGROUP_VOTE)
	// Fallback, if all threads in a subgroup (even from unrelated quads) need to discard, do it.
	if (subgroupAll(to_discard))
		discard;
#endif
}

#endif