#version 450

layout(location = 0) in uvec4 ATTR0;
layout(location = 1) in uvec2 ATTR1;
layout(location = 0) out mediump vec3 vNormal;
layout(location = 1) out mediump vec4 vTangent;
layout(location = 2) out vec2 vUV;

#include "meshlet_attribute_decode.h"

layout(set = 1, binding = 0) uniform UBO
{
    mat4 VP;
};

void main()
{
    vec3 pos = attribute_decode_snorm_exp_position(ATTR0.xy);
    vNormal = attribute_decode_oct8_normal_tangent(ATTR0.z).xyz;
    vTangent = attribute_decode_oct8_normal_tangent(ATTR0.w);
    vUV = attribute_decode_snorm_exp_uv(ATTR1);
    gl_Position = VP * vec4(pos, 1.0);
}
