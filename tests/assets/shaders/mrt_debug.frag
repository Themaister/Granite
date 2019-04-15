#version 450
layout(location = 0) in vec2 vUV;
layout(set = 0, binding = 0) uniform sampler2D uMRT0;
layout(set = 0, binding = 1) uniform sampler2D uMRT1;
layout(set = 0, binding = 2) uniform sampler2D uMRT2;
layout(set = 0, binding = 3) uniform sampler2D uMRT3;
layout(location = 0) out vec4 FragColor;

void main()
{
    if (vUV.x < 0.5)
    {
        if (vUV.y < 0.5)
            FragColor = textureLod(uMRT0, vUV, 0.0);
        else
            FragColor = textureLod(uMRT2, vUV, 0.0);
    }
    else
    {
        if (vUV.y < 0.5)
            FragColor = textureLod(uMRT1, vUV, 0.0);
        else
            FragColor = textureLod(uMRT3, vUV, 0.0);
    }
}
