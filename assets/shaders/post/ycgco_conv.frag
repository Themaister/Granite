#version 450
layout(set = 0, binding = 0, input_attachment_index = 0) uniform subpassInput uHDR;
layout(location = 0) out vec3 YCgCo;

#include "reprojection.h"

void main()
{
    vec3 result = RGB_to_YCgCo(Tonemap(subpassLoad(uHDR).rgb));
    YCgCo = result;
}