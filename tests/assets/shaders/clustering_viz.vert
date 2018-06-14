#version 450
layout(location = 0) in vec4 Position;
layout(location = 1) in vec4 OffsetScale;
layout(location = 2) in vec4 ColorLevel;

layout(location = 0) flat out vec3 vColor;
layout(location = 1) out vec3 vPos;

layout(push_constant, std430) uniform Registers
{
    mat4 VP;
} registers;

void main()
{
    uint level = uint(ColorLevel.w);

    float mod = 1.0;
    if ((0.5 * abs(OffsetScale.x) > abs(OffsetScale.z)) || (0.5 * abs(OffsetScale.y) > abs(OffsetScale.z)))
        mod = 0.1;

    gl_Position = registers.VP * vec4(((Position.xyz * vec3(1.0, 1.0, level == 0u ? 2.0 : 1.0) * OffsetScale.w) + vec3(1.0, 1.0, 2.0) * OffsetScale.xyz), 1.0);
    vColor = mod * ColorLevel.xyz;
    vPos = Position.xyz;
}
