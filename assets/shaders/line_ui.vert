#version 450

layout(location = 0) in highp vec3 Position;

#if HAVE_VERTEX_COLOR
layout(location = 1) in mediump vec4 Color;
layout(location = 0) out mediump vec4 vColor;
#endif

layout(std140, set = 0, binding = 0) uniform Scene
{
    vec3 inv_resolution;
    vec3 pos_offset_pixels;
};

#include "inc/prerotate.h"

void main()
{
    vec2 pos_ndc = (Position.xy + pos_offset_pixels.xy) * inv_resolution.xy;
    gl_Position = vec4(2.0 * pos_ndc - 1.0, (Position.z + pos_offset_pixels.z) * inv_resolution.z, 1.0);
#if HAVE_VERTEX_COLOR
    vColor = Color;
#endif
    prerotate_fixup_clip_xy();
}
