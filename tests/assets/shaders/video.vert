#version 450

layout(set = 3, binding = 0) uniform UBO
{
    mat4 MVP;
};

layout(location = 0) out vec2 vUV;

void main()
{
    float x = float(gl_VertexIndex & 1) * 2.0 - 1.0;
    float z = float(gl_VertexIndex & 2) - 1.0;
    vUV = vec2(x, z) * 0.5 + 0.5;
    gl_Position = MVP * vec4(x, 0.0, z, 1.0);
}