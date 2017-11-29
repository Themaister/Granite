#version 450

#ifdef SHADOW_RESOLVE_VSM
#include "inc/render_parameters.h"
layout(location = 0) out vec2 VSM;
#endif

void main()
{
#ifdef SHADOW_RESOLVE_VSM
    #ifdef DIRECTIONAL_SHADOW_VSM
        float z = gl_FragCoord.z;
    #else
        float z = clip_z_to_linear(gl_FragCoord.z);
    #endif
    VSM = vec2(z, z * z);
#endif
}