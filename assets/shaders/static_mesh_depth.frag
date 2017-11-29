#version 450
precision highp float;

#if HAVE_UV && defined(ALPHA_TEST)
layout(location = 1) in highp vec2 vUV;
#endif

#if (defined(HAVE_BASECOLORMAP) && HAVE_BASECOLORMAP) && defined(ALPHA_TEST)
layout(set = 2, binding = 0) uniform mediump sampler2D uBaseColormap;
#endif

#ifdef ALPHA_TEST_ALPHA_TO_COVERAGE
layout(location = 0) out highp vec4 FragColor;
#elif defined(SHADOW_RESOLVE_VSM)
layout(location = 0) out highp vec2 VSM;
#endif

#ifdef SHADOW_RESOLVE_VSM
#include "inc/render_parameters.h"
#endif

void main()
{
#if defined(HAVE_BASECOLORMAP) && HAVE_BASECOLORMAP && defined(ALPHA_TEST)
    mediump float base_alpha = texture(uBaseColormap, vUV).a;

    #if !defined(ALPHA_TEST_ALPHA_TO_COVERAGE)
        if (base_alpha < 0.5)
            discard;
    #endif

    #ifdef SHADOW_RESOLVE_VSM
        #ifdef DIRECTIONAL_SHADOW_VSM
            float z = gl_FragCoord.z;
        #else
            float z = clip_z_to_linear(gl_FragCoord.z);
        #endif
    #endif

    #if defined(ALPHA_TEST_ALPHA_TO_COVERAGE)
        #ifdef SHADOW_RESOLVE_VSM
            FragColor = vec4(z, z * z, 0.0, base_alpha);
        #else
            FragColor = vec4(0.0, 0.0, 0.0, base_alpha);
        #endif
    #elif defined(SHADOW_RESOLVE_VSM)
        VSM = vec2(z, z * z);
    #endif
#endif
}