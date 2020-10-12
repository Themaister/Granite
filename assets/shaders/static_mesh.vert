#version 450

#if defined(MULTIVIEW) && MULTIVIEW
#extension GL_EXT_multiview : require
#endif

#include "inc/render_parameters.h"

layout(location = 0) in highp vec3 Position;

#ifndef RENDERER_DEPTH
layout(location = 0) out highp vec3 vPos;
#endif

#ifdef RENDERER_DEPTH
    #if HAVE_UV && defined(ALPHA_TEST)
        layout(location = 1) in highp vec2 UV;
        layout(location = 1) out highp vec2 vUV;
    #endif
#else
    #if HAVE_UV
        layout(location = 1) in highp vec2 UV;
        layout(location = 1) out highp vec2 vUV;
    #endif
#endif

#ifndef RENDERER_DEPTH
#if HAVE_NORMAL
layout(location = 2) in mediump vec3 Normal;
layout(location = 2) out mediump vec3 vNormal;
#endif

#if HAVE_TANGENT
layout(location = 3) in mediump vec4 Tangent;
layout(location = 3) out mediump vec4 vTangent;
#endif
#endif

#if HAVE_BONE_INDEX
layout(location = 4) in mediump uvec4 BoneIndices;
#endif

#if HAVE_BONE_WEIGHT
layout(location = 5) in mediump vec4 BoneWeights;
#endif

#if HAVE_VERTEX_COLOR
layout(location = 6) in mediump vec4 VertexColor;
layout(location = 4) out mediump vec4 vColor;
#endif

#if HAVE_BONE_INDEX && HAVE_BONE_WEIGHT
layout(std140, set = 3, binding = 1) uniform BonesWorld
{
    mat4 BoneWorldTransforms[256];
};
#else
struct StaticMeshInfo
{
    mat4 Model;
};

layout(set = 3, binding = 0, std140) uniform PerVertexData
{
    StaticMeshInfo infos[256];
};
#endif

invariant gl_Position;

void main()
{
#if HAVE_BONE_INDEX && HAVE_BONE_WEIGHT
    vec3 world_col0 =
        BoneWorldTransforms[BoneIndices.x][0].xyz * BoneWeights.x +
        BoneWorldTransforms[BoneIndices.y][0].xyz * BoneWeights.y +
        BoneWorldTransforms[BoneIndices.z][0].xyz * BoneWeights.z +
        BoneWorldTransforms[BoneIndices.w][0].xyz * BoneWeights.w;

    vec3 world_col1 =
        BoneWorldTransforms[BoneIndices.x][1].xyz * BoneWeights.x +
        BoneWorldTransforms[BoneIndices.y][1].xyz * BoneWeights.y +
        BoneWorldTransforms[BoneIndices.z][1].xyz * BoneWeights.z +
        BoneWorldTransforms[BoneIndices.w][1].xyz * BoneWeights.w;

    vec3 world_col2 =
        BoneWorldTransforms[BoneIndices.x][2].xyz * BoneWeights.x +
        BoneWorldTransforms[BoneIndices.y][2].xyz * BoneWeights.y +
        BoneWorldTransforms[BoneIndices.z][2].xyz * BoneWeights.z +
        BoneWorldTransforms[BoneIndices.w][2].xyz * BoneWeights.w;

    vec3 world_col3 =
        BoneWorldTransforms[BoneIndices.x][3].xyz * BoneWeights.x +
        BoneWorldTransforms[BoneIndices.y][3].xyz * BoneWeights.y +
        BoneWorldTransforms[BoneIndices.z][3].xyz * BoneWeights.z +
        BoneWorldTransforms[BoneIndices.w][3].xyz * BoneWeights.w;

#else
    vec3 world_col0 = infos[gl_InstanceIndex].Model[0].xyz;
    vec3 world_col1 = infos[gl_InstanceIndex].Model[1].xyz;
    vec3 world_col2 = infos[gl_InstanceIndex].Model[2].xyz;
    vec3 world_col3 = infos[gl_InstanceIndex].Model[3].xyz;
#endif
    vec3 World =
        world_col0 * Position.x +
        world_col1 * Position.y +
        world_col2 * Position.z +
        world_col3;

#if defined(MULTIVIEW) && MULTIVIEW
    gl_Position = global.multiview_view_projection[gl_ViewIndex] * vec4(World, 1.0);
#else
    gl_Position = global.view_projection * vec4(World, 1.0);
#endif

#ifndef RENDERER_DEPTH
    vPos = World;
#endif

#ifndef RENDERER_DEPTH
#if HAVE_NORMAL
    mat3 NormalTransform = mat3(world_col0, world_col1, world_col2);
    #if HAVE_BONE_INDEX && HAVE_BONE_WEIGHT
        vNormal = normalize(NormalTransform * Normal);
        #if HAVE_TANGENT
            vTangent = vec4(normalize(NormalTransform * Tangent.xyz), Tangent.w);
        #endif
    #else
        vNormal = normalize(NormalTransform * Normal);
        #if HAVE_TANGENT
            vTangent = vec4(normalize(NormalTransform * Tangent.xyz), Tangent.w);
        #endif
    #endif
#endif
#endif

#ifdef RENDERER_DEPTH
    #if HAVE_UV && defined(ALPHA_TEST)
        vUV = UV;
    #endif
#else
    #if HAVE_UV
        vUV = UV;
    #endif
#endif

#if HAVE_VERTEX_COLOR
    vColor = VertexColor;
#endif
}
