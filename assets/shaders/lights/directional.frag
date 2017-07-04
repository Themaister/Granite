#version 310 es
precision mediump float;

layout(input_attachment_index = 0, set = 1, binding = 0) uniform mediump subpassInput BaseColor;
layout(input_attachment_index = 1, set = 1, binding = 1) uniform mediump subpassInput Normal;
layout(input_attachment_index = 2, set = 1, binding = 2) uniform mediump subpassInput PBR;
layout(input_attachment_index = 3, set = 1, binding = 3) uniform highp subpassInput Depth;
layout(location = 0) out vec3 FragColor;
layout(location = 0) in highp vec4 vClip;

layout(std430, push_constant) uniform Registers
{
    mat4 inverse_view_projection;
    vec3 direction;
    vec3 color;
} registers;

void main()
{
    highp float depth = subpassLoad(Depth).x;
    highp vec4 clip = vClip + depth * registers.inverse_view_projection[2];
    highp vec3 pos = clip.xyz / clip.w;

    vec3 normal = normalize(subpassLoad(Normal).xyz * 2.0 - 1.0);
    vec3 base_color = subpassLoad(BaseColor).rgb;

    float ndotl = clamp(dot(normal, registers.direction), 0.0, 1.0);
    FragColor = (0.8 * ndotl + 0.2) * base_color;
}