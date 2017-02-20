#version 310 es
precision mediump float;
layout(location = 0) out vec4 Color;
layout(input_attachment_index = 0, set = 0, binding = 0) uniform highp subpassInput uDepth;

#include "basic.inc"

void main()
{
   Color = get_color() + 0.2;
}
