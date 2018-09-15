#version 450
precision highp float;
precision highp int;

#include "fog.h"

layout(std430, push_constant) uniform Registers
{
    mat4 inverse_view_projection;
    vec3 camera_pos;
    vec3 color;
    float falloff;
} registers;

layout(input_attachment_index = 3, set = 3, binding = 3) uniform highp subpassInput Depth;
layout(location = 0) in highp vec4 vClip;
layout(location = 0) out mediump vec4 FragColor;

void main()
{
    float depth = subpassLoad(Depth).x;
    vec4 clip = vClip + depth * registers.inverse_view_projection[2];
    vec3 pos = clip.xyz / clip.w;
    vec3 eye_vec = pos - registers.camera_pos;
    FragColor = vec4(registers.color, fog_factor(eye_vec, registers.falloff));
}
