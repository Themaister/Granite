#ifndef SUBGROUP_EXTENSIONS_H_
#define SUBGROUP_EXTENSIONS_H_

#ifdef SUBGROUP_OPS
#extension GL_KHR_shader_subgroup_basic : require
#extension GL_KHR_shader_subgroup_vote : require
#extension GL_KHR_shader_subgroup_clustered : require
#extension GL_KHR_shader_subgroup_ballot : require
#extension GL_KHR_shader_subgroup_quad : require
#extension GL_KHR_shader_subgroup_arithmetic : require
#endif

#ifdef SUBGROUP_SHUFFLE
#extension GL_KHR_shader_subgroup_shuffle : require
#endif

#endif