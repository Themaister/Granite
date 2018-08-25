#version 450
layout(location = 0) in vec4 Position;
layout(location = 0) out mediump vec4 vColor;
layout(location = 1) out vec2 vUV;

layout(std430, push_constant) uniform UBO
{
    mat4 MVP;
};

void main()
{
    gl_Position = MVP * Position;
    vUV = Position.xy * 7.5 + 0.5;
    vColor = vec4(1.0);
}