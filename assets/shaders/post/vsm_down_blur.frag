#version 310 es

#if defined(LAYERED) && LAYERED
#extension GL_EXT_multiview : require
#endif

precision highp float;

#if defined(LAYERED) && LAYERED
layout(set = 0, binding = 0) uniform highp sampler2DArray uSampler;
#else
layout(set = 0, binding = 0) uniform sampler2D uSampler;
#endif
layout(location = 0) in highp vec2 vUV;
layout(location = 0) out vec2 FragColor;

layout(push_constant, std430) uniform Registers
{
    vec2 inv_texel_size;
} registers;

void main()
{
#if defined(LAYERED) && LAYERED
    float layer = float(gl_ViewIndex);
    #define SAMPLE_OFFSET(x, y) textureLod(uSampler, vec3(vUV + vec2(x, y) * registers.inv_texel_size, layer), 0.0).xy
#else
    #define SAMPLE_OFFSET(x, y) textureLod(uSampler, vUV + vec2(x, y) * registers.inv_texel_size, 0.0).xy
#endif

    vec2 value = 0.25 * SAMPLE_OFFSET(0.0, 0.0);
    value += 0.0625 * SAMPLE_OFFSET(-1.75, +1.75);
    value += 0.125 * SAMPLE_OFFSET(+0.00, +1.75);
    value += 0.0625 * SAMPLE_OFFSET(+1.75, +1.75);
    value += 0.125 * SAMPLE_OFFSET(-1.75, +0.00);
    value += 0.125 * SAMPLE_OFFSET(+1.75, +0.00);
    value += 0.0625 * SAMPLE_OFFSET(-1.75, -1.75);
    value += 0.125 * SAMPLE_OFFSET(+0.00, -1.75);
    value += 0.0625 * SAMPLE_OFFSET(+1.75, -1.75);

    FragColor = value;
}