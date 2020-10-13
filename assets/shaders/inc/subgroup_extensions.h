#ifndef SUBGROUP_EXTENSIONS_H_
#define SUBGROUP_EXTENSIONS_H_

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

#ifdef SUBGROUP_ARITHMETIC
#extension GL_KHR_shader_subgroup_arithmetic : require
#endif

#endif