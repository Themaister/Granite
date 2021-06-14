#version 450

layout(location = 0) out vec2 MV;
layout(location = 0) in vec3 vOldClip;
layout(location = 1) in vec3 vNewClip;

void main()
{
    if (vOldClip.z <= 0.00001)
    {
        MV = vec2(0.0);
    }
    else
    {
        vec2 UV = vNewClip.xy / vNewClip.z;
        vec2 oldUV = vOldClip.xy / vOldClip.z;
        MV = 0.5 * (UV - oldUV);
    }
}