#version 450
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 0) uniform sampler2D uImage;

void main()
{
    FragColor = textureLod(uImage, vUV, 0.0);
}