#version 450
precision highp float;
precision highp int;

#if defined(VARIANT_BIT_0) && defined(HAVE_BASECOLORMAP) && HAVE_BASECOLORMAP
#define BANDLIMITED_PIXEL
#include "inc/bandlimited_pixel_filter.h"
const int bandlimited_pixel_lod = 0;
#endif

layout(location = 0) out mediump vec4 Color;

#if HAVE_VERTEX_COLOR
layout(location = 0) flat in mediump vec4 vColor;
#endif

#if defined(HAVE_UV) && HAVE_UV
layout(location = 1) in highp vec2 vTex;
#endif

#if defined(HAVE_BASECOLORMAP) && HAVE_BASECOLORMAP
layout(set = 2, binding = 0) uniform mediump sampler2D uTex;
#endif

void main()
{
#if HAVE_VERTEX_COLOR
    mediump vec4 color = vColor;
#else
    mediump vec4 color = vec4(1.0);
#endif

#if defined(HAVE_BASECOLORMAP) && HAVE_BASECOLORMAP
    #ifdef BANDLIMITED_PIXEL
        vec2 size = textureSize(uTex, bandlimited_pixel_lod);
        BandlimitedPixelInfo info = compute_pixel_weights(vTex, size, 1.0 / size);
        color *= sample_bandlimited_pixel(uTex, vTex, info, float(bandlimited_pixel_lod));
    #else
        color *= texture(uTex, vTex);
    #endif
    #if defined(ALPHA_TEST)
        if (color.a < 0.5)
            discard;
    #endif
#endif
    Color = color;
}
