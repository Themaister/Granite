#version 310 es
precision mediump float;

#if HAVE_UV
layout(location = 0) in highp vec2 vUV;
#endif

#if HAVE_NORMAL
layout(location = 1) in mediump vec3 vNormal;
#endif

#if HAVE_TANGENT
layout(location = 2) in mediump vec3 vTangent;
#endif

#if HAVE_BASECOLORMAP
layout(set = 2, binding = 0) uniform sampler2D uBaseColormap;
#endif

#if HAVE_NORMALMAP
layout(set = 2, binding = 1) uniform sampler2D uNormalmap;
#endif

#if HAVE_METALLICROUGHNESSMAP
layout(set = 2, binding = 2) uniform sampler2D uMetallicRoughnessmap;
#endif

layout(std430, push_constant) uniform Constants
{
    vec4 base_color;
    float emissive;
    float roughness;
    float metallic;
} registers;

layout(location = 0) out vec4 FragColor;

void main()
{
#if HAVE_BASECOLORMAP
    vec4 base_color = texture(uBaseColormap, vUV);
    #if defined(ALPHA_TEST) && !defined(ALPHA_TEST_ALPHA_TO_COVERAGE)
        if (base_color.a < 0.5)
            discard;
    #endif
#else
    vec4 base_color = registers.base_color;
#endif

#if HAVE_NORMAL
    vec3 normal = normalize(vNormal);
    #if HAVE_NORMALMAP
        vec3 tangent = normalize(vTangent);
        vec3 binormal = cross(normal, tangent);
        vec3 tangent_space = texture(uNormalmap, vUV).xyz * 2.0 - 1.0;
        normal = mat3(tangent, binormal, normal) * tangent_space;
    #endif
#endif

#if HAVE_METALLICROUGHNESSMAP
    vec2 mr = texture(uMetallicRoughnessmap, vUV).xy;
    float metallic = mr.x;
    float roughness = mr.y;
#else
    float metallic = registers.metallic;
    float roughness = registers.roughness;
#endif

    FragColor = base_color;
}