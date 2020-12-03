#version 450
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_samplerless_texture_functions : require

layout(set = 0, binding = 0) uniform texture2D uImages[];
layout(set = 2, binding = 0) uniform texture2D uImages2[];
layout(set = 1, binding = 2) uniform sampler uSampler;
layout(location = 0) out vec4 FragColor;

void main()
{
    int x = int(gl_FragCoord.x / 16.0) & 1023;
    int y = int(gl_FragCoord.y / 16.0) & 1023;
    int index = x ^ y;

    FragColor =
        textureLod(nonuniformEXT(sampler2D(uImages[index], uSampler)), vec2(0.5), 0.0) *
        textureLod(nonuniformEXT(sampler2D(uImages2[index], uSampler)), vec2(0.5), 0.0);
}
