#ifndef HELPER_INVOCATION_H_
#define HELPER_INVOCATION_H_

#ifdef DEMOTE
#extension GL_EXT_demote_to_helper_invocation : require
#endif

bool is_helper_invocation()
{
#if defined(DEMOTE)
	return helperInvocationEXT();
#else
	return gl_HelperInvocation;
#endif
}

#endif