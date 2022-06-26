#version 450

layout(set = 0, binding = 0, input_attachment_index = 0) uniform subpassInput uDepth;

layout(set = 1, binding = 0, std140) uniform UBO
{
    mat4 reprojection;
    vec2 inv_resolution;
};

layout(location = 0) out vec2 MV;

void main()
{
    vec4 clip = vec4(2.0 * gl_FragCoord.xy * inv_resolution - 1.0, subpassLoad(uDepth).x, 1.0);
    vec4 reclip = reprojection * clip;

    if (reclip.w <= 0.0)
    {
        MV = vec2(0.0);
    }
    else
    {
        vec2 oldUV = reclip.xy / reclip.w;
        vec2 UV = clip.xy;
        MV = 0.5 * (UV - oldUV);
    }
}