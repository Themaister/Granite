#version 450
precision highp float;

#if defined(HAVE_EMISSIVE) && HAVE_EMISSIVE
layout(set = 2, binding = 0) uniform sampler2D uSkybox;
#endif
layout(location = 0) in highp vec3 vDirection;

layout(location = 0) out vec3 Emissive;

layout(std430, push_constant) uniform Registers
{
    vec3 color;
} registers;

void main()
{
#if defined(HAVE_EMISSIVE) && HAVE_EMISSIVE
    vec3 v = normalize(vDirection);
    if (abs(v.x) < 0.00001)
        v.x = 0.00001;

    vec2 uv = vec2(atan(v.z, v.x), asin(-v.y));
    uv *= vec2(0.1591, 0.3183);
    uv += 0.5;
    Emissive = textureLod(uSkybox, uv, 0.0).rgb * registers.color;
#else
    Emissive = registers.color;
#endif
}
