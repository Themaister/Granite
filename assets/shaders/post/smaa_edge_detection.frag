#version 450
precision highp float;
precision highp int;

#define SMAA_INCLUDE_VS 0
#define SMAA_INCLUDE_PS 1
#include "smaa_common.h"

layout(set = 0, binding = 0) uniform sampler2D ColorTex;
layout(location = 0) in vec2 vTex;
layout(location = 1) in vec4 vOffset0;
layout(location = 2) in vec4 vOffset1;
layout(location = 3) in vec4 vOffset2;
layout(location = 0) out vec2 Edges;

void main()
{
    Edges = SMAALumaEdgeDetectionPS(vTex, vec4[](vOffset0, vOffset1, vOffset2), ColorTex);
}
