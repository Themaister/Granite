#version 450
#define SMAA_INCLUDE_VS 1
#define SMAA_INCLUDE_PS 0
#include "smaa_common.h"

layout(location = 0) in vec2 Position;
layout(location = 0) out vec2 vTex;
layout(location = 1) out vec4 vOffset0;
layout(location = 2) out vec4 vOffset1;
layout(location = 3) out vec4 vOffset2;

#include "../inc/prerotate.h"

void main()
{
    vec2 TexCoord = Position * 0.5 + 0.5;
    gl_Position = vec4(Position, 0.0, 1.0);

    vec4 out_coord[3];
    SMAAEdgeDetectionVS(TexCoord, out_coord);
    vTex = TexCoord;
    vOffset0 = out_coord[0];
    vOffset1 = out_coord[1];
    vOffset2 = out_coord[2];
    prerotate_fixup_clip_xy();
}