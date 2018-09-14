#version 450
layout(location = 0) in vec2 Position;
layout(location = 0) out vec2 vUV;

void main()
{
    gl_Position = vec4(Position, 1.0, 1.0);
    vUV = Position * 0.5 + 0.5;
}