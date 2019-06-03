#version 450
layout(location = 0) in vec2 vUV;
layout(set = 0, binding = 0) uniform sampler2DArray uMultiview;
layout(location = 0) out vec4 FragColor;

void main()
{
    if (vUV.x < 0.5)
    {
        if (vUV.y < 0.5)
            FragColor = textureLod(uMultiview, vec3(vUV, 0.0), 0.0);
        else
            FragColor = textureLod(uMultiview, vec3(vUV, 1.0), 0.0);
    }
    else
    {
        if (vUV.y < 0.5)
            FragColor = textureLod(uMultiview, vec3(vUV, 2.0), 0.0);
        else
            FragColor = textureLod(uMultiview, vec3(vUV, 3.0), 0.0);
    }
}