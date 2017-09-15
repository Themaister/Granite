#version 450
precision mediump float;

layout(location = 0) out vec4 Color;

#if HAVE_VERTEX_COLOR
layout(location = 0) flat in mediump vec4 vColor;
#endif

#if defined(HAVE_UV) && HAVE_UV
layout(location = 1) in highp vec2 vTex;
#endif

#if defined(HAVE_BASECOLORMAP) && HAVE_BASECOLORMAP
layout(set = 2, binding = 0) uniform sampler2D uTex;
#endif

void main()
{
#if HAVE_VERTEX_COLOR
    vec4 color = vColor;
#else
    vec4 color = vec4(1.0);
#endif

#if defined(HAVE_BASECOLORMAP) && HAVE_BASECOLORMAP
    color *= texture(uTex, vTex);
    #if defined(ALPHA_TEST)
        if (color.a < 0.5)
            discard;
    #endif
#endif
    Color = color;
}
