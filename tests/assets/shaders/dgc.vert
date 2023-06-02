#version 450

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aOffset;

void main()
{
    gl_Position = vec4(aPos + aOffset, 0.0, 1.0);
}