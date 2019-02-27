#version 450
precision highp float;
precision highp int;

#ifdef ALPHA_TEST_DISABLE
#undef ALPHA_TEST
#endif

#if defined(VARIANT_BIT_0) && VARIANT_BIT_0 && defined(HAVE_BASECOLORMAP) && HAVE_BASECOLORMAP && defined(ALPHA_TEST)
#define BANDLIMITED_PIXEL
#include "inc/bandlimited_pixel_filter.h"
const int bandlimited_pixel_lod = 0;
#endif

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
#if defined(HAVE_BASECOLORMAP) && HAVE_BASECOLORMAP && defined(ALPHA_TEST) && defined(HAVE_UV)
    #if defined(BANDLIMITED_PIXEL)
        vec2 size = vec2(textureSize(uBaseColormap, bandlimited_pixel_lod));
        BandlimitedPixelInfo info = compute_pixel_weights(vUV, size, 1.0 / size, 1.0);
        mediump float base_alpha = sample_bandlimited_pixel(uBaseColormap, vUV, info, float(bandlimited_pixel_lod)).a;
    #else
        mediump float base_alpha = texture(uBaseColormap, vUV).a;
    #endif

    #if !defined(ALPHA_TEST_ALPHA_TO_COVERAGE)
        if (base_alpha < 0.5)
            discard;
    #endif
#else
    const mediump float base_alpha = 1.0;
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
}