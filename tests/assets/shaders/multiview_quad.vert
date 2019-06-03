#version 450
#extension GL_EXT_multiview : require

layout(std140, binding = 0) uniform UBO
{
    vec4 BasePositions[4];
};

layout(location = 0) in vec2 Quad;

void main()
{
    vec2 pos = BasePositions[gl_ViewIndex].xy;
    pos.x += float(gl_InstanceIndex) * 0.03;
    pos += Quad;
    gl_Position = vec4(pos, 0.0, 1.0);
}
