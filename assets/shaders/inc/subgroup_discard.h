#ifndef SUBGROUP_DISCARD_H_
#define SUBGROUP_DISCARD_H_

#include "helper_invocation.h"
#include "subgroup_extensions.h"

void quad_discard_late(bool to_discard)
{
#if !defined(DEMOTE)
	if (to_discard)
		discard;
#endif
}

void quad_discard_early(bool to_discard)
{
#if defined(DEMOTE)
	if (to_discard)
		demote;
#elif defined(SUBGROUP_OPS)
	// Next best solution. Broadcast all lanes in the quad and decide.
	bvec4 lanes = bvec4(
			subgroupQuadBroadcast(to_discard, 0),
			subgroupQuadBroadcast(to_discard, 1),
			subgroupQuadBroadcast(to_discard, 2),
			subgroupQuadBroadcast(to_discard, 3));
	if (all(lanes))
		discard;
#endif
}

#endif