#version 450
layout(location = 0) out mediump float FragColor;
layout(location = 0) in vec2 vUV;
layout(set = 0, binding = 0) uniform mediump sampler2D uSSAO;

void main()
{
    mediump float c0 = textureLodOffset(uSSAO, vUV, 0.0, ivec2(-1, -1)).x;
    mediump float c1 = textureLodOffset(uSSAO, vUV, 0.0, ivec2(+1, -1)).x;
    mediump float c2 = textureLodOffset(uSSAO, vUV, 0.0, ivec2(-1, +1)).x;
    mediump float c3 = textureLodOffset(uSSAO, vUV, 0.0, ivec2(+1, +1)).x;
    FragColor = 0.25 * (c0 + c1 + c2 + c3);
}