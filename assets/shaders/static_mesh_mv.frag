#version 450

#include "lights/lighting_data.h"

layout(location = 0) out vec2 MV;
layout(location = 0) in vec3 vOldClip;

void main()
{
    if (vOldClip.w <= 0.00001)
    {
        MV = vec2(0.0);
    }
    else
    {
        vec2 UV = gl_FragCoord.xy * resolution.inv_resolution;
        vec2 old_window_pos = 0.5 * (vOldClip.xy / vOldClip.w) + 0.5;
        MV = UV - old_window_pos;
    }
}