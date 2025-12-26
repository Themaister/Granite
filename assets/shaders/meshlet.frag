#version 460
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_mesh_shader : require

#include "inc/render_target.h"
#include "inc/two_component_normal.h"
#include "inc/global_bindings.h"

#if defined(ALPHA_TEST)
#include "inc/helper_invocation.h"
#endif

layout(location = 0) in mediump vec3 vNormal;
layout(location = 1) in mediump vec4 vTangent;
layout(location = 3) in vec3 vPos;

layout(location = 4) perprimitiveEXT flat in uint vMaterialID;
layout(location = 2) in vec2 vUV;

layout(set = 2, binding = 0) uniform mediump texture2D uImages[];
layout(set = 0, binding = BINDING_GLOBAL_GEOMETRY_SAMPLER) uniform mediump sampler uSamp;

void main()
{
    uint tex_index = vMaterialID & 0xffffu;
    mediump vec4 base_color = texture(nonuniformEXT(sampler2D(uImages[tex_index], uSamp)), vUV);
#if defined(ALPHA_TEST)
    if (base_color.a < 0.5)
        demote;
#endif

    mediump vec3 normal = normalize(vNormal);
    mediump vec3 tangent = normalize(vTangent.xyz);
    mediump vec3 binormal = cross(normal, tangent) * vTangent.w;
    mediump vec2 tangent_space = texture(nonuniformEXT(sampler2D(uImages[tex_index + 1], uSamp)), vUV).xy * 2.0 - 1.0;

    // For 2-component compressed textures.
    normal = normalize(mat3(tangent, binormal, normal) * two_component_normal(tangent_space));

    if (!gl_FrontFacing)
        normal = -normal;

    mediump vec2 mr = texture(nonuniformEXT(sampler2D(uImages[tex_index + 2], uSamp)), vUV).bg;
    mediump float metallic = mr.x;
    mediump float roughness = mr.y;

    const mediump float ambient = 1.0;
    const mediump vec3 emissive = vec3(0.0);

    emit_render_target(emissive, base_color, normal, metallic, roughness, ambient, vPos);
}
