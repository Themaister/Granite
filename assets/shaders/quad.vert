#version 310 es
layout(location = 0) in mediump ivec2 QuadCoord;
layout(location = 1) in ivec4 PosOffsetScale;
layout(location = 2) in ivec4 TexOffsetScale;
layout(location = 3) in mediump vec4 Color;
layout(location = 4) in mediump float Layer;

layout(std140, set = 0, binding = 0) uniform Scene
{
    vec2 inv_resolution;
    vec2 pos_offset_pixels;
};

layout(location = 0) flat out mediump vec4 vColor;
#if defined(HAVE_UV) && HAVE_UV
layout(location = 1) out highp vec2 vTex;
layout(push_constant, std430) uniform Globals
{
    vec2 inv_tex_resolution;
} constants;
#endif

void main()
{
    ivec2 pos_pixels = QuadCoord * PosOffsetScale.zw + PosOffsetScale.xy;
    vec2 pos_ndc = (vec2(pos_pixels) + pos_offset_pixels) * inv_resolution;
    gl_Position = vec4(2.0 * pos_ndc - 1.0, Layer, 1.0);

#if defined(HAVE_UV) && HAVE_UV
    vTex = vec2(QuadCoord * TexOffsetScale.zw + TexOffsetScale.xy) * constants.inv_tex_resolution;
#endif
    vColor = Color;
}
