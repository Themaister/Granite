#version 310 es
precision mediump float;

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
layout(set = 2, binding = 0) uniform sampler2D uBaseColormap;
#endif

#if defined(HAVE_NORMALMAP) && HAVE_NORMALMAP
layout(set = 2, binding = 1) uniform sampler2D uNormalmap;
#endif

#if defined(HAVE_METALLICROUGHNESSMAP) && HAVE_METALLICROUGHNESSMAP
layout(set = 2, binding = 2) uniform sampler2D uMetallicRoughnessmap;
#endif

#if defined(HAVE_OCCLUSIONMAP) && HAVE_OCCLUSIONMAP
layout(set = 2, binding = 3) uniform sampler2D uOcclusionMap;
#endif

#if defined(HAVE_EMISSIVEMAP) && HAVE_EMISSIVEMAP
layout(set = 2, binding = 4) uniform sampler2D uEmissiveMap;
#endif

layout(std430, push_constant) uniform Constants
{
    vec4 base_color;
    vec4 emissive;
    float roughness;
    float metallic;
    float lod_bias;
} registers;

#include "inc/render_target.h"

#if defined(ALPHA_TEST) && !defined(ALPHA_TEST_ALPHA_TO_COVERAGE)
#define NEED_GRADIENTS
#endif

void main()
{
#ifdef NEED_GRADIENTS
    vec2 gradX = dFdx(vUV);
    vec2 gradY = dFdy(vUV);
#endif

#if defined(HAVE_BASECOLORMAP) && HAVE_BASECOLORMAP
    #ifdef NEED_GRADIENTS
        vec4 base_color = textureGrad(uBaseColormap, vUV, gradX, gradY) * registers.base_color;
    #else
        vec4 base_color = texture(uBaseColormap, vUV, registers.lod_bias) * registers.base_color;
    #endif
#else
    vec4 base_color = registers.base_color;
#endif

#if HAVE_VERTEX_COLOR
    base_color *= vColor;
#endif

    // Ideally we want to discard ASAP, so we need to take explicit gradients first.
#if defined(ALPHA_TEST) && !defined(ALPHA_TEST_ALPHA_TO_COVERAGE)
    if (base_color.a < 0.5)
        discard;
#endif

#if defined(HAVE_NORMAL) && HAVE_NORMAL
    vec3 normal = normalize(vNormal);
    #if defined(HAVE_NORMALMAP) && HAVE_NORMALMAP
        vec3 tangent = normalize(vTangent.xyz);
        vec3 binormal = cross(normal, tangent) * vTangent.w;
        #ifdef NEED_GRADIENTS
            vec3 tangent_space = textureGrad(uNormalmap, vUV, gradX, gradY).xyz * 2.0 - 1.0;
        #else
            vec3 tangent_space = texture(uNormalmap, vUV, registers.lod_bias).xyz * 2.0 - 1.0;
        #endif
        normal = normalize(mat3(tangent, binormal, normal) * tangent_space);
    #endif
    if (!gl_FrontFacing)
        normal = -normal;
#endif

#if defined(HAVE_METALLICROUGHNESSMAP) && HAVE_METALLICROUGHNESSMAP
    #ifdef NEED_GRADIENTS
        vec2 mr = textureGrad(uMetallicRoughnessmap, vUV, gradX, gradY).bg;
    #else
        vec2 mr = texture(uMetallicRoughnessmap, vUV, registers.lod_bias).bg;
    #endif
    float metallic = mr.x * registers.metallic;
    float roughness = mr.y * registers.roughness;
#else
    float metallic = registers.metallic;
    float roughness = registers.roughness;
#endif

#if defined(HAVE_OCCLUSIONMAP) && HAVE_OCCLUSIONMAP
    #ifdef NEED_GRADIENTS
        float ambient = texture(uOcclusionMap, vUV, gradX, gradY).x;
    #else
        float ambient = texture(uOcclusionMap, vUV, registers.lod_bias).x;
    #endif
#else
    const float ambient = 1.0;
#endif

#if defined(HAVE_EMISSIVEMAP) && HAVE_EMISSIVEMAP
    #ifdef NEED_GRADIENTS
        vec3 emissive = texture(uEmissiveMap, vUV, gradX, gradY).rgb;
    #else
        vec3 emissive = texture(uEmissiveMap, vUV, registers.lod_bias).rgb;
    #endif
    emissive *= registers.emissive.rgb;
#else
    vec3 emissive = registers.emissive.rgb;
#endif

    emit_render_target(emissive, base_color, normal, metallic, roughness, ambient, vEyeVec);
}
