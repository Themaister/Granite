#version 450

layout(location = 0) in vec2 Attr;
layout(location = 0) out vec2 vUV;

void main()
{
    gl_Position = vec4(Attr, 1.0, 1.0);
    vUV = Attr * 0.5 + 0.5;
}