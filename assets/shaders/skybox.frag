#version 310 es
precision mediump float;

#if defined(HAVE_EMISSIVEMAP) && defined(HAVE_EMISSIVEMAP)
layout(set = 2, binding = 0) uniform samplerCube uSkybox;
#endif
layout(location = 0) in highp vec3 vDirection;

layout(location = 0) out vec3 Emissive;

layout(std430, push_constant) uniform Registers
{
    vec3 color;
} registers;

void main()
{
#if defined(HAVE_EMISSIVEMAP) && defined(HAVE_EMISSIVEMAP)
    Emissive = texture(uSkybox, vDirection).rgb * registers.color;
#else
    Emissive = registers.color;
#endif
}
