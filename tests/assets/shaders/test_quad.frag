#version 450
layout(location = 0) out vec4 FragColor;
layout(location = 0) in vec2 vQuad;

layout(push_constant, std430) uniform Registers
{
    vec4 color;
    float phase;
} registers;

void main()
{
    vec2 absquad = abs(vQuad);
    vec2 feather = smoothstep(vec2(0.9), vec2(1.0), absquad);
    float f = (1.0 - feather.x) * (1.0 - feather.y);
    FragColor = registers.color * vec4(vec3(1.0), f);
}