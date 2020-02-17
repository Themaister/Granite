#version 450

layout(location = 0) out vec4 FragColor;
layout(location = 0) in vec4 vColor;

layout(constant_id = 0) const int iterations = 1000;

vec4 heavy_computation(vec4 c)
{
    for (int i = 0; i < iterations; i++)
    {
        c = sin(c);
        c = cos(c);
        c += dFdx(c);
        c += dFdy(c);
    }
    return c;
}

void main()
{
    FragColor = heavy_computation(vColor);
}
