#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 1) uniform sampler2D uSampler;
layout(set = 0, binding = 0, std140) uniform Weights
{
    vec4 weights[4];
};

void main()
{
    vec3 tex = vec3(0.0);
    float lod = -10.0;
    vec2 uv = vUV;
    if (weights[int(gl_FragCoord.x) + 2 * int(gl_FragCoord.y)].x > 0.0)
    {
        tex = texture(uSampler, uv).rgb;
        lod = textureQueryLod(uSampler, uv).y;
    }

    FragColor = vec4(tex, lod);
}