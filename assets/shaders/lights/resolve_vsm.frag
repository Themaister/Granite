#version 450

layout(input_attachment_index = 0, set = 0, binding = 0) uniform subpassInputMS uDepth;
layout(location = 0) out vec2 FragColor;

void main()
{
    float value = subpassLoad(uDepth, gl_SampleID).x;
    FragColor = vec2(value, value * value);
}