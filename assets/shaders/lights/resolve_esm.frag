#version 450

layout(input_attachment_index = 0, set = 0, binding = 0) uniform subpassInputMS uDepth;
layout(location = 0) out float FragColor;

void main()
{
    float value = subpassLoad(uDepth, gl_SampleID).x;
    FragColor = exp2(100.0 * value);
}
