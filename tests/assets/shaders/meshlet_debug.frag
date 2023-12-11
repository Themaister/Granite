#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in mediump vec3 vNormal;
layout(location = 1) in mediump vec4 vTangent;
layout(location = 2) in vec2 vUV;
layout(location = 3) flat in uint MaterialOffset;

layout(location = 0) out vec3 FragColor;

layout(set = 0, binding = 2) uniform sampler DefaultSampler;
layout(set = 2, binding = 0) uniform texture2D Textures[];

void main()
{
    vec3 color = texture(sampler2D(Textures[MaterialOffset], DefaultSampler), vUV).rgb;
    FragColor = color * dot(vNormal.xyz, vec3(4, 2, 3));
}
