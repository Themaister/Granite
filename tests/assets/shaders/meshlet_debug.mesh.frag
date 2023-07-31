#version 450
#extension GL_EXT_mesh_shader : require

layout(location = 0) perprimitiveEXT in flat uint vMeshletIndex;
layout(location = 1) in mediump vec3 vNormal;
layout(location = 2) in mediump vec4 vTangent;
layout(location = 3) in vec2 vUV;

layout(location = 0) out vec4 FragColor;

vec3 decode_mesh_color()
{
    uint index = vMeshletIndex * 1991u;
    index ^= (index >> 5u);
    uint r = bitfieldExtract(index, 0, 2);
    uint g = bitfieldExtract(index, 2, 2);
    uint b = bitfieldExtract(index, 4, 2);
    return vec3(r, g, b) / 3.0;
}

void main()
{
    FragColor = vec4(decode_mesh_color() * (vNormal.xyz * 0.5 + 0.5), 1.0);
}