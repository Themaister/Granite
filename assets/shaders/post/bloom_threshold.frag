#version 310 es
precision mediump float;

layout(std140, set = 0, binding = 1) uniform LuminanceData
{
    float average_log_luminance;
    float average_linear_luminance;
    float average_inv_linear_luminance;
};

layout(set = 0, binding = 0) uniform sampler2D uHDR;
layout(location = 0) out vec4 FragColor;
layout(location = 0) in highp vec2 vUV;

void main()
{
    vec3 color = textureLod(uHDR, vUV, 0.0).rgb;
    highp float luminance = dot(color + 0.00001, vec3(0.29, 0.60, 0.11));
    highp float loglum = log2(luminance);

    color /= luminance;
    luminance -= 4.0 * average_linear_luminance;
    vec3 thres_color = max(color * luminance, vec3(0.0));
    FragColor = vec4(thres_color, loglum);
}