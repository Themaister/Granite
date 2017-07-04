#version 310 es

layout(location = 0) in vec2 Position;

void main()
{
    gl_Position = vec4(0.5 * Position, 0.0, 1.0);
}