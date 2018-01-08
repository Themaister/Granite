#version 450
precision highp float;
precision highp int;

#define SMAA_INCLUDE_VS 0
#define SMAA_INCLUDE_PS 1
#include "smaa_common.h"

layout(set = 0, binding = 0) uniform sampler2D EdgesTex;
layout(set = 0, binding = 1) uniform sampler2D AreaTex;
layout(set = 0, binding = 2) uniform sampler2D SearchTex;

layout(location = 0) in vec2 vTex;
layout(location = 1) in vec2 vPixCoord;
layout(location = 2) in vec4 vOffset0;
layout(location = 3) in vec4 vOffset1;
layout(location = 4) in vec4 vOffset2;
layout(location = 0) out vec4 Weights;

void main()
{
    Weights = SMAABlendingWeightCalculationPS(vTex, vPixCoord, vec4[](vOffset0, vOffset1, vOffset2),
                                              EdgesTex, AreaTex, SearchTex,
                                          #if SMAA_SUBPIXEL_MODE == 0
                                              vec4(0.0)
                                          #elif SMAA_SUBPIXEL_MODE == 1
                                              vec4(1.0, 1.0, 1.0, 0.0)
                                          #elif SMAA_SUBPIXEL_MODE == 2
                                              vec4(2.0, 2.0, 2.0, 0.0)
                                          #endif
                                              );
}