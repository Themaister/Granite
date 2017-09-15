#version 450
precision highp float;

layout(location = 0) in highp vec2 vUV;
layout(set = 0, binding = 0) uniform sampler2D uSampler;
layout(location = 0) out vec4 FragColor;

#define METHOD_3TAP_GAUSS_HORIZ 0
#define METHOD_5TAP_GAUSS_HORIZ 1
#define METHOD_7TAP_GAUSS_HORIZ 2
#define METHOD_3TAP_GAUSS_VERT 3
#define METHOD_5TAP_GAUSS_VERT 4
#define METHOD_7TAP_GAUSS_VERT 5
#define METHOD_3x3_TAP_GAUSS 6

#ifndef METHOD
#define METHOD METHOD_3x3_TAP_GAUSS
#endif

void main()
{
    vec4 color = vec4(0.0);
#if METHOD == METHOD_3TAP_GAUSS_HORIZ
    const float w0 = 0.25;
    const float w1 = 0.5;
    color += textureLodOffset(uSampler, vUV, 0.0, ivec2(-1, 0)) * w0;
    color += textureLod(uSampler, vUV, 0.0) * w1;
    color += textureLodOffset(uSampler, vUV, 0.0, ivec2(+1, 0)) * w0;
#elif METHOD == METHOD_5TAP_GAUSS_HORIZ
    const float w0 = 0.07142857;
    const float w1 = 0.14285;
    const float w2 = 0.285714;
	const float weight = 1.0 / (w2 + w1 * 2.0 + w0 * 2.0);
	const float W0 = w0 * weight;
	const float W1 = w1 * weight;
	const float W2 = w2 * weight;
    color += textureLodOffset(uSampler, vUV, 0.0, ivec2(-2, 0)) * W0;
    color += textureLodOffset(uSampler, vUV, 0.0, ivec2(-1, 0)) * W1;
    color += textureLod(uSampler, vUV, 0.0) * W2;
    color += textureLodOffset(uSampler, vUV, 0.0, ivec2(+1, 0)) * W1;
    color += textureLodOffset(uSampler, vUV, 0.0, ivec2(+2, 0)) * W0;
#elif METHOD == METHOD_7TAP_GAUSS_HORIZ
    const float w0 = 0.03007964045641579;
    const float w1 = 0.10496581333665146;
    const float w2 = 0.22226040083356216;
    const float w3 = 0.2853882907467413;
    color += textureLodOffset(uSampler, vUV, 0.0, ivec2(-3, 0)) * w0;
    color += textureLodOffset(uSampler, vUV, 0.0, ivec2(-2, 0)) * w1;
    color += textureLodOffset(uSampler, vUV, 0.0, ivec2(-1, 0)) * w2;
    color += textureLod(uSampler, vUV, 0.0) * w3;
    color += textureLodOffset(uSampler, vUV, 0.0, ivec2(+1, 0)) * w2;
    color += textureLodOffset(uSampler, vUV, 0.0, ivec2(+2, 0)) * w1;
    color += textureLodOffset(uSampler, vUV, 0.0, ivec2(+3, 0)) * w0;
#elif METHOD == METHOD_3TAP_GAUSS_VERT
    const float w0 = 0.25;
    const float w1 = 0.5;
    color += textureLodOffset(uSampler, vUV, 0.0, ivec2(0, -1)) * w0;
    color += textureLod(uSampler, vUV, 0.0) * w1;
    color += textureLodOffset(uSampler, vUV, 0.0, ivec2(0, +1)) * w0;
#elif METHOD == METHOD_5TAP_GAUSS_VERT
    const float w0 = 0.07142857;
    const float w1 = 0.14285;
    const float w2 = 0.285714;
	const float weight = 1.0 / (w2 + w1 * 2.0 + w0 * 2.0);
	const float W0 = w0 * weight;
	const float W1 = w1 * weight;
	const float W2 = w2 * weight;
    color += textureLodOffset(uSampler, vUV, 0.0, ivec2(0, -2)) * W0;
    color += textureLodOffset(uSampler, vUV, 0.0, ivec2(0, -1)) * W1;
    color += textureLod(uSampler, vUV, 0.0) * W2;
    color += textureLodOffset(uSampler, vUV, 0.0, ivec2(0, +1)) * W1;
    color += textureLodOffset(uSampler, vUV, 0.0, ivec2(0, +2)) * W0;
#elif METHOD == METHOD_7TAP_GAUSS_VERT
    const float w0 = 0.03007964045641579;
    const float w1 = 0.10496581333665146;
    const float w2 = 0.22226040083356216;
    const float w3 = 0.2853882907467413;
    color += textureLodOffset(uSampler, vUV, 0.0, ivec2(0, -3)) * w0;
    color += textureLodOffset(uSampler, vUV, 0.0, ivec2(0, -2)) * w1;
    color += textureLodOffset(uSampler, vUV, 0.0, ivec2(0, -1)) * w2;
    color += textureLod(uSampler, vUV, 0.0) * w3;
    color += textureLodOffset(uSampler, vUV, 0.0, ivec2(0, +1)) * w2;
    color += textureLodOffset(uSampler, vUV, 0.0, ivec2(0, +2)) * w1;
    color += textureLodOffset(uSampler, vUV, 0.0, ivec2(0, +3)) * w0;
#elif METHOD == METHOD_3x3_TAP_GAUSS
    const float w0 = 0.0625;
    const float w1 = 0.125;
    const float w2 = 0.25;
    color += textureLodOffset(uSampler, vUV, 0.0, ivec2(-1, -1)) * w0;
    color += textureLodOffset(uSampler, vUV, 0.0, ivec2(+0, -1)) * w1;
    color += textureLodOffset(uSampler, vUV, 0.0, ivec2(+1, -1)) * w0;
    color += textureLodOffset(uSampler, vUV, 0.0, ivec2(-1, +0)) * w1;
    color += textureLod(uSampler, vUV, 0.0) * w2;
    color += textureLodOffset(uSampler, vUV, 0.0, ivec2(+1, +0)) * w1;
    color += textureLodOffset(uSampler, vUV, 0.0, ivec2(-1, +1)) * w0;
    color += textureLodOffset(uSampler, vUV, 0.0, ivec2(+0, +1)) * w1;
    color += textureLodOffset(uSampler, vUV, 0.0, ivec2(+1, +1)) * w0;
#endif
    FragColor = color;
}
