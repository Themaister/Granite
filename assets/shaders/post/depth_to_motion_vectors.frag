#version 450

layout(set = 0, binding = 0, input_attachment_index = 0) uniform subpassInput uDepth;

layout(std430, push_constant) uniform Registers
{
    mat4 reproj;
} registers;

layout(location = 0) out vec2 MV;
layout(location = 0) in vec2 vUV;

void main()
{
    float depth = subpassLoad(uDepth).x;
    vec4 clip = vec4(vUV * 2.0 - 1.0, depth, 1.0);
    vec4 reproj = registers.reproj * clip;

    if (reproj.w > 0.0)
    {
        vec2 uv = reproj.xy / reproj.w;
        MV = vUV - uv;
    }
    else
    {
        // Clipped, can't get a sensible MV.
        MV = vec2(0.0);
    }
}