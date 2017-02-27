#version 310 es
precision mediump float;
layout(location = 0) in vec2 Position;
layout(location = 0) out mediump vec2 vTex;
void main()
{
   gl_Position = vec4(Position, 0.0, 1.0);
   vTex = Position * 0.5 + 0.5;
}
