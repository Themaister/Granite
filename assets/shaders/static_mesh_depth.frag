#version 310 es
precision mediump float;

#if HAVE_UV
layout(location = 0) in highp vec2 vUV;
#endif

#if HAVE_ALBEDOMAP
layout(set = 2, binding = 0) uniform sampler2D uAlbedomap;
#endif

void main()
{
#if HAVE_ALBEDOMAP
    vec4 albedo = texture(uAlbedomap, vUV);
    #if ALPHA_TEST && !ALPHA_TEST_ALPHA_TO_COVERAGE
        if (albedo.a < 0.5)
            discard;
    #endif
#endif
}