#version 310 es
precision mediump float;

layout(set = 0, binding = 0, std140) uniform UBO
{
    float value;
};

layout(location = 0) out vec4 FragColor;

void main()
{
    FragColor = vec4(value);
}