#version 310 es
layout(location = 0) in mediump vec2 QuadCoord;
layout(location = 1) in vec4 PosOffsetScale;
layout(location = 2) in vec4 TexOffsetScale;
layout(location = 3) in mediump vec4 Rotation;
#if HAVE_VERTEX_COLOR
layout(location = 4) in mediump vec4 Color;
#endif
layout(location = 5) in mediump float Layer;

layout(std140, set = 0, binding = 0) uniform Scene
{
    vec3 inv_resolution;
    vec3 pos_offset_pixels;
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
    vec2 QuadPos = (mat2(Rotation.xy, Rotation.zw) * QuadCoord) * 0.5 + 0.5;
    vec2 pos_pixels = QuadPos * PosOffsetScale.zw + PosOffsetScale.xy;
    vec2 pos_ndc = (pos_pixels + pos_offset_pixels.xy) * inv_resolution.xy;
    gl_Position = vec4(2.0 * pos_ndc - 1.0, Layer * inv_resolution.z + pos_offset_pixels.z, 1.0);

#if defined(HAVE_UV) && HAVE_UV
    vTex = ((QuadCoord * 0.5 + 0.5) * TexOffsetScale.zw + TexOffsetScale.xy) * constants.inv_tex_resolution;
#endif
#if HAVE_VERTEX_COLOR
    vColor = Color;
#endif
}
