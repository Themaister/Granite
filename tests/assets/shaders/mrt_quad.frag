#version 450
layout(location = 1) out vec4 MRT1;
layout(location = 2) out vec4 MRT2;
layout(location = 3) out vec4 MRT3;

void main()
{
    MRT1 = vec4(0.0, 1.0, 0.0, 1.0);
    MRT2 = vec4(0.0, 1.0, 1.0, 1.0);
    MRT3 = vec4(1.0, 0.0, 1.0, 1.0);
}
