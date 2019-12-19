#ifndef CLUSTERER_H_
#define CLUSTERER_H_

#ifdef CLUSTERING_WAVE_UNIFORM
#extension GL_KHR_shader_subgroup_arithmetic : require
#extension GL_KHR_shader_subgroup_ballot : require
#endif

#ifdef CLUSTERER_BINDLESS
#extension GL_EXT_nonuniform_qualifier : require
#endif

#include "clusterer_data.h"

#ifdef CLUSTERER_BINDLESS
#include "clusterer_bindless.h"
#else
#include "clusterer_legacy.h"
#endif

#endif
