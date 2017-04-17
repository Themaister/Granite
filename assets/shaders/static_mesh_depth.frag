#version 310 es
precision mediump float;

#if HAVE_UV
layout(location = 0) in highp vec2 vUV;
#endif

#if defined(HAVE_BASECOLORMAP) && HAVE_BASECOLORMAP
layout(set = 2, binding = 0) uniform sampler2D uBaseColormap;
#endif

void main()
{
#if HAVE_ALBEDOMAP
    vec4 base_color = texture(uBaseColormap, vUV);
    #if defined(ALPHA_TEST) && !defined(ALPHA_TEST_ALPHA_TO_COVERAGE)
        if (base_color.a < 0.5)
            discard;
    #endif
#endif
}