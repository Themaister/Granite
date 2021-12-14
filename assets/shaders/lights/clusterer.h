#ifndef CLUSTERER_H_
#define CLUSTERER_H_

#ifdef CLUSTERER_BINDLESS
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_samplerless_texture_functions : require
#endif

#include "../inc/subgroup_extensions.h"
#include "clusterer_data.h"

#ifdef CLUSTERER_BINDLESS
#include "clusterer_bindless.h"
#else
#include "clusterer_legacy.h"
#endif

#endif
