#version 450
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_samplerless_texture_functions : require

layout(set = 0, binding = 0) uniform texture2D uImages[];
layout(set = 2, binding = 0) uniform texture2D uImages2[];
layout(location = 0) out vec4 FragColor;

void main()
{
    int x = int(gl_FragCoord.x / 16.0) & 1023;
    int y = int(gl_FragCoord.y / 16.0) & 1023;
    int index = x ^ y;

    FragColor =
        texelFetch(uImages[nonuniformEXT(index)], ivec2(0), 0) *
        texelFetch(uImages2[nonuniformEXT(index)], ivec2(0), 0);
}