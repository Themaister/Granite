#version 460
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_mesh_shader : require

#ifdef ALPHA_TEST_DISABLE
#undef ALPHA_TEST
#endif

#ifdef RENDERER_FORWARD
#include "inc/subgroup_extensions.h"
#endif

#if defined(ALPHA_TEST) || defined(RENDERER_FORWARD)
#include "inc/helper_invocation.h"
#endif

#ifndef RENDERER_DEPTH
#include "inc/render_target.h"
#endif

#include "inc/two_component_normal.h"
#include "inc/global_bindings.h"
#include "inc/meshlet_render_types.h"

#if !defined(RENDERER_DEPTH)
#define ATTR_LEVEL 2
#elif defined(ALPHA_TEST)
#define ATTR_LEVEL 1
#else
#define ATTR_LEVEL 0
#endif

#if ATTR_LEVEL >= 1
layout(location = 0) in vec2 vUV;
layout(location = 1) perprimitiveEXT flat in uint vMaterialFlags;
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
    uint tex_index = bitfieldExtract(vMaterialFlags,
        MESH_ASSET_MATERIAL_TEXTURE_INDEX_OFFSET,
        MESH_ASSET_MATERIAL_TEXTURE_INDEX_BITS);
    uint samp_index = bitfieldExtract(vMaterialFlags, MESH_ASSET_MATERIAL_UV_CLAMP_OFFSET, 1);

    mediump vec4 base_color = vec4(1.0);
    if ((vMaterialFlags & MESH_ASSET_MATERIAL_BASE_COLOR_BIT) != 0)
    {
        base_color = texture(nonuniformEXT(sampler2D(uImages[tex_index], uSamp[samp_index])), vUV);
#if defined(ALPHA_TEST)
        if (base_color.a < 0.5)
            demote;
#endif
        tex_index++;
    }
#endif

#if ATTR_LEVEL >= 2
    mediump vec3 normal = normalize(vNormal);
    mediump float metallic = 0.0;
    mediump float roughness = 1.0;
    mediump float ambient = 1.0;
    mediump vec3 emissive = vec3(0.0);

    if ((vMaterialFlags & MESH_ASSET_MATERIAL_NORMAL_BIT) != 0)
    {
        mediump vec3 tangent = normalize(vTangent.xyz);
        mediump vec3 binormal = cross(normal, tangent) * vTangent.w;
        mediump vec2 tangent_space = texture(nonuniformEXT(sampler2D(uImages[tex_index], uSamp[samp_index])), vUV).xy * 2.0 - 1.0;

        // For 2-component compressed textures.
        normal = normalize(mat3(tangent, binormal, normal) * two_component_normal(tangent_space));
        tex_index++;
    }

    if ((vMaterialFlags & MESH_ASSET_MATERIAL_METALLIC_ROUGHNESS_BIT) != 0)
    {
        mediump vec2 mr = texture(nonuniformEXT(sampler2D(uImages[tex_index], uSamp[samp_index])), vUV).bg;
        metallic = mr.x;
        roughness = mr.y;
        tex_index++;
    }

    if ((vMaterialFlags & MESH_ASSET_MATERIAL_OCCLUSION_BIT) != 0)
    {
        ambient = texture(nonuniformEXT(sampler2D(uImages[tex_index], uSamp[samp_index])), vUV).x;
        tex_index++;
    }

    if ((vMaterialFlags & MESH_ASSET_MATERIAL_EMISSIVE_BIT) != 0)
        emissive = texture(nonuniformEXT(sampler2D(uImages[tex_index], uSamp[samp_index])), vUV).rgb;
#endif

#ifndef RENDERER_DEPTH
    emit_render_target(emissive, base_color, normal, metallic, roughness, ambient, vPos);
#endif
}
