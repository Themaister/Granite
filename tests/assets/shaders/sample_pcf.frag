#version 450
layout(location = 0) out vec4 FragColor;
layout(location = 0) in vec2 vUV;
layout(set = 0, binding = 0) uniform sampler2D uPlain;
layout(set = 1, binding = 1) uniform sampler2DShadow uPCF;

void main()
{
    vec4 clip = vec4(vUV, 1.0, 2.0);
    FragColor = mix(texture(uPlain, vUV).xxxx, vec4(textureProjLod(uPCF, clip, 0.0)), 0.9);
}