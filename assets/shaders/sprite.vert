#version 450

layout(location = 0) in mediump vec2 QuadCoord;
layout(location = 1) in vec4 PosOffsetScale;
layout(location = 2) in vec4 TexOffsetScale;
layout(location = 3) in mediump vec4 Rotation;
#if HAVE_VERTEX_COLOR
layout(location = 4) in mediump vec4 Color;
#endif
layout(location = 5) in mediump float Layer;

#if defined(VARIANT_BIT_1) && VARIANT_BIT_1 && defined(HAVE_BASECOLORMAP) && HAVE_BASECOLORMAP
#define SPRITE_BLENDING
#endif

#if defined(VARIANT_BIT_5) && VARIANT_BIT_5
#define ARRAY_LAYERS
#endif

#ifdef SPRITE_BLENDING
layout(location = 6) in mediump float BlendFactor;
#endif

#ifdef ARRAY_LAYERS
layout(location = 7) in float ArrayLayer;
#endif

layout(std140, set = 0, binding = 0) uniform Scene
{
    vec3 inv_resolution;
    vec3 pos_offset_pixels;
};

layout(location = 0) flat out mediump vec4 vColor;
#if defined(HAVE_UV) && HAVE_UV
    #ifdef SPRITE_BLENDING
        layout(location = 1) out highp vec3 vTex;
    #else
        layout(location = 1) out highp vec2 vTex;
    #endif
    #ifdef ARRAY_LAYERS
        layout(location = 2) flat out highp float vLayer;
    #endif
    layout(set = 3, binding = 0, std140) uniform Globals
    {
        vec2 tex_resolution;
        vec2 inv_tex_resolution;
    } constants;
#endif

#include "inc/prerotate.h"

void main()
{
    vec2 QuadPos = (mat2(Rotation.xy, Rotation.zw) * QuadCoord) * 0.5 + 0.5;
    vec2 pos_pixels = QuadPos * PosOffsetScale.zw + PosOffsetScale.xy;
    vec2 pos_ndc = (pos_pixels + pos_offset_pixels.xy) * inv_resolution.xy;
    gl_Position = vec4(2.0 * pos_ndc - 1.0, (Layer + pos_offset_pixels.z) * inv_resolution.z, 1.0);

#if defined(HAVE_UV) && HAVE_UV
    vec2 uv = ((QuadCoord * 0.5 + 0.5) * TexOffsetScale.zw + TexOffsetScale.xy) * constants.inv_tex_resolution;
    #ifdef SPRITE_BLENDING
        vTex = vec3(uv, BlendFactor);
    #else
        vTex = uv;
    #endif
    #ifdef ARRAY_LAYERS
        vLayer = ArrayLayer;
    #endif
#endif

#if HAVE_VERTEX_COLOR
    vColor = Color;
#endif
    prerotate_fixup_clip_xy();
}
