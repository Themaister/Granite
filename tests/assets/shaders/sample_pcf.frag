#version 450
layout(location = 0) out vec4 FragColor;
layout(location = 0) in vec2 vUV;
layout(set = 0, binding = 0) uniform sampler2DShadow uPCF;

void main()
{
    FragColor = vec4(texture(uPCF, vec3(vUV, 0.5)));
}