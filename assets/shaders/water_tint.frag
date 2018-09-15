#version 450
precision highp float;

layout(std430, push_constant) uniform Registers
{
    mat4 inverse_view_projection;
    vec3 falloff;
} registers;

layout(input_attachment_index = 3, set = 3, binding = 3) uniform highp subpassInput Depth;
layout(location = 0) in highp vec4 vClip;
layout(location = 0) out mediump vec3 FragColor;

void main()
{
    float depth = subpassLoad(Depth).x;
    vec4 clip = vClip + depth * registers.inverse_view_projection[2];
    vec3 pos = clip.xyz / clip.w;
    vec3 pos_near = vClip.xyz / vClip.w;
    float distance = distance(pos, pos_near);
    FragColor = exp2(-registers.falloff * distance);
}
