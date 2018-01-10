#version 310 es
precision mediump float;

layout(set = 0, binding = 0) uniform mediump sampler2D uHDR;
layout(set = 0, binding = 1) uniform mediump sampler2D uBloom;

layout(std140, set = 0, binding = 2) uniform LuminanceData
{
    float average_log_luminance;
    float average_linear_luminance;
    float average_inv_linear_luminance;
};

#define SHARPEN 0
#if SHARPEN
layout(std430, push_constant) uniform Registers
{
    vec2 inv_resolution;
} registers;
#endif

layout(location = 0) in highp vec2 vUV;
layout(location = 0) out mediump vec3 FragColor;

const mediump float A = 0.15;
const mediump float B = 0.50;
const mediump float C = 0.10;
const mediump float D = 0.20;
const mediump float E = 0.02;
const mediump float F = 0.30;
const mediump float W = 11.2;

mediump vec3 Uncharted2Tonemap(mediump vec3 x)
{
   return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

mediump float Uncharted2Tonemap(mediump float x)
{
   return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

mediump vec3 tonemap_filmic(mediump vec3 color)
{
   mediump vec3 curr = Uncharted2Tonemap(color);
   mediump float white_scale = 1.0 / Uncharted2Tonemap(W);
   return curr * white_scale;
}

mediump vec3 tonemap_reinhart(mediump vec3 color)
{
    color = color / (1.0 + color);
    return color;
}

void main()
{
    mediump vec3 color = textureLod(uHDR, vUV, 0.0).rgb;
    #if SHARPEN
        color *= 2.0;
        color -= 0.25 * textureLod(uHDR, vUV + registers.inv_resolution * vec2(-0.5, -0.5), 0.0).rgb;
        color -= 0.25 * textureLod(uHDR, vUV + registers.inv_resolution * vec2(+0.5, -0.5), 0.0).rgb;
        color -= 0.25 * textureLod(uHDR, vUV + registers.inv_resolution * vec2(-0.5, +0.5), 0.0).rgb;
        color -= 0.25 * textureLod(uHDR, vUV + registers.inv_resolution * vec2(+0.5, +0.5), 0.0).rgb;
        color = max(color, 0.0);
    #endif
    mediump vec3 bloom = textureLod(uBloom, vUV, 0.0).rgb;
    color += bloom;
    FragColor = tonemap_filmic(color * average_inv_linear_luminance);
    //FragColor = 0.25 * color * average_inv_linear_luminance;
}
