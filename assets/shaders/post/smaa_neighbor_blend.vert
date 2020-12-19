#version 450
#define SMAA_INCLUDE_VS 1
#define SMAA_INCLUDE_PS 0
#include "smaa_common.h"

layout(location = 0) in vec2 Position;
layout(location = 0) out vec2 vTex;
layout(location = 1) out vec4 vOffset;

#include "../inc/prerotate.h"

void main()
{
    vec2 TexCoord = Position * 0.5 + 0.5;
    gl_Position = vec4(Position, 0.0, 1.0);
    SMAANeighborhoodBlendingVS(TexCoord, vOffset);
    vTex = TexCoord;
    prerotate_fixup_clip_xy();
}