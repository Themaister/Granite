#version 450

#include "../inc/srgb.h"

layout(location = 0) out vec4 FragColor;
layout(location = 0) in vec2 vUV;
layout(set = 0, binding = 0) uniform sampler2D uTex;

layout(push_constant) uniform Registers
{
    vec2 resolution;
    vec2 inv_resolution;
} registers;

#define PI 3.1415628

// Simple and naive sinc upscaler, slow impl.
// Placeholder implementation.

float sinc(float v)
{
    if (abs(v) < 0.0001)
    {
        return 1.0;
    }
    else
    {
        v *= PI;
        return sin(v) / v;
    }
}

float kernel(float v)
{
    return sinc(v) * sinc(v * 0.5);
}

float weight(float x, float y)
{
    return kernel(x) * kernel(y);
}

void main()
{
    vec2 coord = vUV * registers.resolution - 0.5;
    vec2 i_coord = floor(coord);
    vec2 f_coord = coord - i_coord;
    vec2 uv = (i_coord + 0.5) * registers.inv_resolution;

    vec3 color = vec3(0.0);

    float total_w = 0.0;

#define TAP(X, Y) { \
    float w = weight(f_coord.x - float(X), f_coord.y - float(Y)); \
    vec3 col = textureLodOffset(uTex, uv, 0.0, ivec2(X, Y)).rgb; \
    color += col * w; \
    total_w += w; \
}

#define TAPS(l) TAP(-1, l); TAP(+0, l); TAP(+1, l); TAP(+2, l)
    TAPS(-1);
    TAPS(+0);
    TAPS(+1);
    TAPS(+2);

    color /= total_w;

#if TARGET_SRGB
    color = decode_srgb(color);
#endif

    FragColor = vec4(color, 1.0);
}