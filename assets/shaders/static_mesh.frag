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

#if HAVE_ALBEDOMAP
layout(set = 2, binding = 0) uniform sampler2D uAlbedomap;
#endif

#if HAVE_NORMALMAP
layout(set = 2, binding = 1) uniform sampler2D uNormalmap;
#endif

#if HAVE_ROUGHNESSMAP
layout(set = 2, binding = 2) uniform sampler2D uRoughnessmap;
#endif

#if HAVE_METALLICMAP
layout(set = 2, binding = 3) uniform sampler2D uMetallicmap;
#endif

layout(std430, push_constant) uniform Constants
{
    vec4 albedo;
    float emissive;
    float roughness;
    float metallic;
} registers;

layout(location = 0) out vec4 FragColor;

void main()
{
#if HAVE_ALBEDOMAP
    vec4 albedo = texture(uAlbedomap, vUV);
    #if ALPHA_TEST && !ALPHA_TEST_ALPHA_TO_COVERAGE
        if (albedo.a < 0.5)
            discard;
    #endif
#else
    vec4 albedo = registers.albedo;
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

#if HAVE_ROUGHNESSMAP
    float roughness = texture(uRoughnessmap, vUV).x;
#else
    float roughness = registers.roughness;
#endif

#if HAVE_METALLICMAP
    float roughness = texture(uMetallicmap, vUV).x;
#else
    float roughness = registers.metallic;
#endif

    FragColor = albedo;
}