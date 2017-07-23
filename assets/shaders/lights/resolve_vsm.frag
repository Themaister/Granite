#version 450

layout(input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput uDepth;
layout(location = 0) out vec2 FragColor;

void main()
{
    float value = subpassLoad(uDepth).x;
    FragColor = vec2(value, value * value);
}
