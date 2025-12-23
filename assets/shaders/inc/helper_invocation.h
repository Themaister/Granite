#ifndef HELPER_INVOCATION_H_
#define HELPER_INVOCATION_H_

#extension GL_EXT_demote_to_helper_invocation : require

bool is_helper_invocation()
{
	return helperInvocationEXT();
}

#define HAS_IS_HELPER_INVOCATION

#endif
