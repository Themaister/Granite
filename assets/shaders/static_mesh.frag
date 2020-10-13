#version 450
precision highp float;
precision highp int;

#ifdef ALPHA_TEST_DISABLE
#undef ALPHA_TEST
#endif

#if defined(RENDERER_FORWARD)
#include "inc/subgroup_extensions.h"
#endif

#if defined(ALPHA_TEST)
#include "inc/subgroup_discard.h"
#else
#include "inc/helper_invocation.h"
#endif

#if defined(VARIANT_BIT_0) && VARIANT_BIT_0 && defined(HAVE_BASECOLORMAP) && HAVE_BASECOLORMAP
#define BANDLIMITED_PIXEL
#include "inc/bandlimited_pixel_filter.h"
const int bandlimited_pixel_lod = 0;
#endif

#include "inc/two_component_normal.h"

layout(location = 0) in highp vec3 vPos;

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
    float normal_scale;
} registers;

#include "inc/render_target.h"

void main()
{
#if defined(BANDLIMITED_PIXEL)
    vec2 size = vec2(textureSize(uBaseColormap, bandlimited_pixel_lod));
    BandlimitedPixelInfo info = compute_pixel_weights(vUV, size, 1.0 / size, 1.0);
#endif

#if defined(HAVE_BASECOLORMAP) && HAVE_BASECOLORMAP
    #if defined(BANDLIMITED_PIXEL)
        mediump vec4 base_color = sample_bandlimited_pixel(uBaseColormap, vUV, info, float(bandlimited_pixel_lod));
    #else
        mediump vec4 base_color = texture(uBaseColormap, vUV) * registers.base_color;
    #endif
#else
    mediump vec4 base_color = registers.base_color;
#endif

#if defined(ALPHA_TEST)
    bool should_discard = is_helper_invocation() ||
    #ifdef ALPHA_TEST_ALPHA_TO_COVERAGE
        (base_color.a == 0.0);
    #else
        (base_color.a < 0.5);
    #endif
    // If all threads in a quad can discard, just kill the quad early, since we won't need derivatives.
    quad_discard_early(should_discard);
#endif

#if HAVE_VERTEX_COLOR
    base_color *= vColor;
#endif

#if defined(HAVE_NORMAL) && HAVE_NORMAL
    mediump vec3 normal = normalize(vNormal);
    #if defined(HAVE_NORMALMAP) && HAVE_NORMALMAP
        mediump vec3 tangent = normalize(vTangent.xyz);
        mediump vec3 binormal = cross(normal, tangent) * vTangent.w;
        #if 0 && defined(BANDLIMITED_PIXEL)
            mediump vec2 tangent_space = sample_bandlimited_pixel(uNormalmap, vUV, info, float(bandlimited_pixel_lod)).xy * 2.0 - 1.0;
        #else
            mediump vec2 tangent_space = texture(uNormalmap, vUV).xy * 2.0 - 1.0;
        #endif

        // For 2-component compressed textures.
        tangent_space *= registers.normal_scale;
        normal = normalize(mat3(tangent, binormal, normal) * two_component_normal(tangent_space));
    #endif
    if (!gl_FrontFacing)
        normal = -normal;
#else
    const vec3 normal = vec3(0.0, 1.0, 0.0);
#endif

#if defined(HAVE_METALLICROUGHNESSMAP) && HAVE_METALLICROUGHNESSMAP
    #if 0 && defined(BANDLIMITED_PIXEL)
        mediump vec2 mr = sample_bandlimited_pixel(uMetallicRoughnessmap, vUV, info, float(bandlimited_pixel_lod)).bg;
    #else
        mediump vec2 mr = texture(uMetallicRoughnessmap, vUV).bg;
    #endif
    mediump float metallic = mr.x * registers.metallic;
    mediump float roughness = mr.y * registers.roughness;
#else
    mediump float metallic = registers.metallic;
    mediump float roughness = registers.roughness;
#endif

#if defined(HAVE_OCCLUSIONMAP) && HAVE_OCCLUSIONMAP
    #if 0 && defined(BANDLIMITED_PIXEL)
        mediump float ambient = sample_bandlimited_pixel(uOcclusionMap, vUV, info, float(bandlimited_pixel_lod)).x;
    #else
        mediump float ambient = texture(uOcclusionMap, vUV).x;
    #endif
#else
    const mediump float ambient = 1.0;
#endif

#if defined(HAVE_EMISSIVEMAP) && HAVE_EMISSIVEMAP
    #if defined(BANDLIMITED_PIXEL)
        mediump vec3 emissive = sample_bandlimited_pixel(uEmissiveMap, vUV, info, float(bandlimited_pixel_lod)).rgb;
    #else
        mediump vec3 emissive = texture(uEmissiveMap, vUV).rgb;
    #endif
    emissive *= registers.emissive.rgb;
#else
    mediump vec3 emissive = registers.emissive.rgb;
#endif

    // Ideally we want to discard ASAP, but discarding early make derivatives undefined.
    // Ideally, we'd ballot early.
#if defined(ALPHA_TEST)
    quad_discard_late(should_discard);
#endif

    emit_render_target(emissive, base_color, normal, metallic, roughness, ambient, vPos);
}
