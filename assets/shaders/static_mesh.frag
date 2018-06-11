#version 450
precision highp float;
precision highp int;

layout(location = 0) in mediump vec3 vEyeVec;

#if HAVE_UV
layout(location = 1) in highp vec2 vUV;
#endif

#if HAVE_NORMAL
layout(location = 2) in mediump vec3 vNormal;
#endif

#if HAVE_TANGENT
layout(location = 3) in mediump vec4 vTangent;
#endif

#if HAVE_VERTEX_COLOR
layout(location = 4) in mediump vec4 vColor;
#endif

#if defined(HAVE_BASECOLORMAP) && HAVE_BASECOLORMAP
layout(set = 2, binding = 0) uniform mediump sampler2D uBaseColormap;
#endif

#if defined(HAVE_NORMALMAP) && HAVE_NORMALMAP
layout(set = 2, binding = 1) uniform mediump sampler2D uNormalmap;
#endif

#if defined(HAVE_METALLICROUGHNESSMAP) && HAVE_METALLICROUGHNESSMAP
layout(set = 2, binding = 2) uniform mediump sampler2D uMetallicRoughnessmap;
#endif

#if defined(HAVE_OCCLUSIONMAP) && HAVE_OCCLUSIONMAP
layout(set = 2, binding = 3) uniform mediump sampler2D uOcclusionMap;
#endif

#if defined(HAVE_EMISSIVEMAP) && HAVE_EMISSIVEMAP
layout(set = 2, binding = 4) uniform mediump sampler2D uEmissiveMap;
#endif

layout(std430, push_constant) uniform Constants
{
    vec4 base_color;
    vec4 emissive;
    float roughness;
    float metallic;
    float lod_bias;
    float normal_scale;
} registers;

#include "inc/render_target.h"

void main()
{
#if defined(HAVE_BASECOLORMAP) && HAVE_BASECOLORMAP
	mediump vec4 base_color = texture(uBaseColormap, vUV, registers.lod_bias) * registers.base_color;
#else
    mediump vec4 base_color = registers.base_color;
#endif

#if HAVE_VERTEX_COLOR
    base_color *= vColor;
#endif

#if defined(HAVE_NORMAL) && HAVE_NORMAL
    mediump vec3 normal = normalize(vNormal);
    #if defined(HAVE_NORMALMAP) && HAVE_NORMALMAP
        mediump vec3 tangent = normalize(vTangent.xyz);
        mediump vec3 binormal = cross(normal, tangent) * vTangent.w;
        mediump vec2 tangent_space = texture(uNormalmap, vUV, registers.lod_bias).xy * 2.0 - 1.0;

        // For 2-component compressed textures.
        mediump float tangent_z = sqrt(max(0.0, 1.0 - dot(tangent_space, tangent_space)));
        tangent_space *= registers.normal_scale;
        normal = normalize(mat3(tangent, binormal, normal) * vec3(tangent_space, tangent_z));
    #endif
    if (!gl_FrontFacing)
        normal = -normal;
#else
    const vec3 normal = vec3(0.0, 1.0, 0.0);
#endif

#if defined(HAVE_METALLICROUGHNESSMAP) && HAVE_METALLICROUGHNESSMAP
    mediump vec2 mr = texture(uMetallicRoughnessmap, vUV, registers.lod_bias).bg;
    mediump float metallic = mr.x * registers.metallic;
    mediump float roughness = mr.y * registers.roughness;
#else
    mediump float metallic = registers.metallic;
    mediump float roughness = registers.roughness;
#endif

#if defined(HAVE_OCCLUSIONMAP) && HAVE_OCCLUSIONMAP
	mediump float ambient = texture(uOcclusionMap, vUV, registers.lod_bias).x;
#else
    const mediump float ambient = 1.0;
#endif

#if defined(HAVE_EMISSIVEMAP) && HAVE_EMISSIVEMAP
	mediump vec3 emissive = texture(uEmissiveMap, vUV, registers.lod_bias).rgb;
    emissive *= registers.emissive.rgb;
#else
    mediump vec3 emissive = registers.emissive.rgb;
#endif

    // Ideally we want to discard ASAP, but discarding early make derivatives undefined.
    // Ideally, we'd ballot early.
#if defined(ALPHA_TEST) && !defined(ALPHA_TEST_ALPHA_TO_COVERAGE)
    if (base_color.a < 0.5)
        discard;
#endif

    emit_render_target(emissive, base_color, normal, metallic, roughness, ambient, vEyeVec);
}
