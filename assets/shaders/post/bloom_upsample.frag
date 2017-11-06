#version 310 es
precision mediump float;

layout(set = 0, binding = 0) uniform mediump sampler2D uSampler;
layout(location = 0) in highp vec2 vUV;
layout(location = 0) out mediump vec4 FragColor;

layout(push_constant, std430) uniform Registers
{
    vec2 inv_texel_size;
} registers;

void main()
{
    mediump vec4 value = 0.25 * textureLod(uSampler, vUV, 0.0);
    value += 0.0625 * textureLod(uSampler, vUV + vec2(-0.875, +0.875) * registers.inv_texel_size, 0.0);
    value += 0.125 * textureLod(uSampler, vUV + vec2(+0.00, +0.875) * registers.inv_texel_size, 0.0);
    value += 0.0625 * textureLod(uSampler, vUV + vec2(+0.875, +0.875) * registers.inv_texel_size, 0.0);
    value += 0.125 * textureLod(uSampler, vUV + vec2(-0.875, +0.00) * registers.inv_texel_size, 0.0);
    value += 0.125 * textureLod(uSampler, vUV + vec2(+0.875, +0.00) * registers.inv_texel_size, 0.0);
    value += 0.0625 * textureLod(uSampler, vUV + vec2(-0.875, -0.875) * registers.inv_texel_size, 0.0);
    value += 0.125 * textureLod(uSampler, vUV + vec2(+0.00, -0.875) * registers.inv_texel_size, 0.0);
    value += 0.0625 * textureLod(uSampler, vUV + vec2(+0.875, -0.875) * registers.inv_texel_size, 0.0);

    FragColor = value;
}