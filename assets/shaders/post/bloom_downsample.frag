#version 310 es
precision mediump float;

layout(set = 0, binding = 0) uniform mediump sampler2D uSampler;
#ifdef FEEDBACK
layout(set = 0, binding = 1) uniform mediump sampler2D uSamplerHistory;
#endif
layout(location = 0) in highp vec2 vUV;
layout(location = 0) out mediump vec4 FragColor;

layout(push_constant, std430) uniform Registers
{
    vec2 inv_texel_size;
#ifdef FEEDBACK
    float lerp;
#endif
} registers;

void main()
{
    mediump vec4 value = 0.25 * textureLod(uSampler, vUV, 0.0);
    value += 0.0625 * textureLod(uSampler, vUV + vec2(-1.75, +1.75) * registers.inv_texel_size, 0.0);
    value += 0.125 * textureLod(uSampler, vUV + vec2(+0.00, +1.75) * registers.inv_texel_size, 0.0);
    value += 0.0625 * textureLod(uSampler, vUV + vec2(+1.75, +1.75) * registers.inv_texel_size, 0.0);
    value += 0.125 * textureLod(uSampler, vUV + vec2(-1.75, +0.00) * registers.inv_texel_size, 0.0);
    value += 0.125 * textureLod(uSampler, vUV + vec2(+1.75, +0.00) * registers.inv_texel_size, 0.0);
    value += 0.0625 * textureLod(uSampler, vUV + vec2(-1.75, -1.75) * registers.inv_texel_size, 0.0);
    value += 0.125 * textureLod(uSampler, vUV + vec2(+0.00, -1.75) * registers.inv_texel_size, 0.0);
    value += 0.0625 * textureLod(uSampler, vUV + vec2(+1.75, -1.75) * registers.inv_texel_size, 0.0);
#ifdef FEEDBACK
    value = mix(textureLod(uSamplerHistory, vUV, 0.0), value, vec4(vec3(registers.lerp), 1.0));
#endif

    FragColor = value;
}