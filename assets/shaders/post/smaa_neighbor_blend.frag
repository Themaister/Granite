#version 450
precision highp float;
precision highp int;
#define SMAA_INCLUDE_VS 0
#define SMAA_INCLUDE_PS 1
#include "smaa_common.h"

layout(set = 0, binding = 0) uniform sampler2D ColorTex;
layout(set = 0, binding = 1) uniform sampler2D BlendTex;

layout(location = 0) in vec2 vTex;
layout(location = 1) in vec4 vOffset;
layout(location = 0) out vec4 Color;

void main()
{
    Color = SMAANeighborhoodBlendingPS(vTex, vOffset, ColorTex, BlendTex);
}
