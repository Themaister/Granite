#version 310 es
precision mediump float;
layout(location = 0) out vec4 Color;
layout(location = 0) in vec2 vTex;
layout(set = 0, binding = 0) uniform sampler2D uTex;

void main()
{
   Color = texture(uTex, vTex);
}
