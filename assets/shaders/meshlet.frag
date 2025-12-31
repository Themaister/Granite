#version 460
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_mesh_shader : require

#ifndef RENDERER_DEPTH
#include "inc/render_target.h"
#endif

#include "inc/two_component_normal.h"
#include "inc/global_bindings.h"

#if !defined(RENDERER_DEPTH)
#define ATTR_LEVEL 2
#elif defined(ALPHA_TEST)
#define ATTR_LEVEL 1
#else
#define ATTR_LEVEL 0
#endif

#if defined(ALPHA_TEST)
#include "inc/helper_invocation.h"
#endif

#if ATTR_LEVEL >= 1
layout(location = 0) in vec2 vUV;
layout(location = 1) perprimitiveEXT flat in uint vMaterialID;
#endif

#if ATTR_LEVEL >= 2
layout(location = 2) in mediump vec3 vNormal;
layout(location = 3) in mediump vec4 vTangent;
layout(location = 4) in vec3 vPos;
#endif

layout(set = 2, binding = 0) uniform mediump texture2D uImages[];
layout(set = 0, binding = BINDING_GLOBAL_GEOMETRY_SAMPLER_BASE) uniform mediump sampler uSamp[2];

void main()
{
#if ATTR_LEVEL >= 1
    uint tex_index = vMaterialID & 0xffffu;

    mediump vec4 base_color = texture(nonuniformEXT(sampler2D(uImages[tex_index], uSamp[0])), vUV);
#if defined(ALPHA_TEST)
    if (base_color.a < 0.5)
        demote;
#endif
#endif

#if ATTR_LEVEL >= 2
    mediump vec3 normal = normalize(vNormal);
    mediump vec3 tangent = normalize(vTangent.xyz);
    mediump vec3 binormal = cross(normal, tangent) * vTangent.w;
    mediump vec2 tangent_space = texture(nonuniformEXT(sampler2D(uImages[tex_index + 1], uSamp[0])), vUV).xy * 2.0 - 1.0;

    // For 2-component compressed textures.
    normal = normalize(mat3(tangent, binormal, normal) * two_component_normal(tangent_space));

    mediump vec2 mr = texture(nonuniformEXT(sampler2D(uImages[tex_index + 2], uSamp[0])), vUV).bg;
    mediump float metallic = mr.x;
    mediump float roughness = mr.y;

    const mediump float ambient = 1.0;
    const mediump vec3 emissive = vec3(0.0);
#endif

#ifndef RENDERER_DEPTH
    emit_render_target(emissive, base_color, normal, metallic, roughness, ambient, vPos);
#endif
}
