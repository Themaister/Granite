#version 310 es
precision mediump float;

layout(set = 0, binding = 0) uniform sampler2D uHDR;
layout(set = 0, binding = 1) uniform sampler2D uBloom;

layout(std140, set = 0, binding = 2) uniform LuminanceData
{
    float average_log_luminance;
    float average_linear_luminance;
    float average_inv_linear_luminance;
};

layout(location = 0) in highp vec2 vUV;
layout(location = 0) out vec3 FragColor;

vec3 tonemap_reinhart(vec3 color)
{
    color = color / (1.0 + color);
    return color;
}

void main()
{
    vec3 color = textureLod(uHDR, vUV, 0.0).rgb;
    vec3 bloom = textureLod(uBloom, vUV, 0.0).rgb;
    color = bloom;
    FragColor = tonemap_reinhart(color * average_inv_linear_luminance);
}