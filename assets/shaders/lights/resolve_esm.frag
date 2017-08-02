#version 450

layout(input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput uDepth;
layout(location = 0) out float FragColor;

void main()
{
    float value = 2.0 * subpassLoad(uDepth).x - 1.0;
    FragColor = exp2(100.0 * value);
}
