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

const float A = 0.15;
const float B = 0.50;
const float C = 0.10;
const float D = 0.20;
const float E = 0.02;
const float F = 0.30;
const float W = 11.2;

vec3 Uncharted2Tonemap(vec3 x)
{
   return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

float Uncharted2Tonemap(float x)
{
   return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

vec3 tonemap_filmic(vec3 color)
{
   vec3 curr = Uncharted2Tonemap(color);
   float white_scale = 1.0 / Uncharted2Tonemap(W);
   return curr * white_scale;
}

vec3 tonemap_reinhart(vec3 color)
{
    color = color / (1.0 + color);
    return color;
}

void main()
{
    vec3 color = textureLod(uHDR, vUV, 0.0).rgb;
    vec3 bloom = textureLod(uBloom, vUV, 0.0).rgb;
    color += bloom;
    //FragColor = tonemap_reinhart(color * average_inv_linear_luminance);
    FragColor = tonemap_filmic(color * average_inv_linear_luminance);
}