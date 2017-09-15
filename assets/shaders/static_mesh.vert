#version 450

#include "inc/render_parameters.h"

layout(location = 0) in highp vec3 Position;

#ifndef RENDERER_DEPTH
layout(location = 0) out mediump vec3 vEyeVec;
#endif

#if HAVE_UV
layout(location = 1) in highp vec2 UV;
layout(location = 1) out highp vec2 vUV;
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

layout(std140, set = 3, binding = 2) uniform BonesNormal
{
    mat4 BoneNormalTransforms[256];
};
#else
struct StaticMeshInfo
{
    mat4 Model;
    mat4 Normal;
};

layout(set = 3, binding = 0, std140) uniform PerVertexData
{
    StaticMeshInfo infos[256];
};
#endif

void main()
{
#if HAVE_BONE_INDEX && HAVE_BONE_WEIGHT
    vec3 World =
        (
        BoneWorldTransforms[BoneIndices.x][0].xyz * BoneWeights.x +
        BoneWorldTransforms[BoneIndices.y][0].xyz * BoneWeights.y +
        BoneWorldTransforms[BoneIndices.z][0].xyz * BoneWeights.z +
        BoneWorldTransforms[BoneIndices.w][0].xyz * BoneWeights.w) * Position.x +

        (
        BoneWorldTransforms[BoneIndices.x][1].xyz * BoneWeights.x +
        BoneWorldTransforms[BoneIndices.y][1].xyz * BoneWeights.y +
        BoneWorldTransforms[BoneIndices.z][1].xyz * BoneWeights.z +
        BoneWorldTransforms[BoneIndices.w][1].xyz * BoneWeights.w) * Position.y +

        (
        BoneWorldTransforms[BoneIndices.x][2].xyz * BoneWeights.x +
        BoneWorldTransforms[BoneIndices.y][2].xyz * BoneWeights.y +
        BoneWorldTransforms[BoneIndices.z][2].xyz * BoneWeights.z +
        BoneWorldTransforms[BoneIndices.w][2].xyz * BoneWeights.w) * Position.z +

        (
        BoneWorldTransforms[BoneIndices.x][3].xyz * BoneWeights.x +
        BoneWorldTransforms[BoneIndices.y][3].xyz * BoneWeights.y +
        BoneWorldTransforms[BoneIndices.z][3].xyz * BoneWeights.z +
        BoneWorldTransforms[BoneIndices.w][3].xyz * BoneWeights.w);
#else
    vec3 World =
        infos[gl_InstanceIndex].Model[0].xyz * Position.x +
        infos[gl_InstanceIndex].Model[1].xyz * Position.y +
        infos[gl_InstanceIndex].Model[2].xyz * Position.z +
        infos[gl_InstanceIndex].Model[3].xyz;
#endif
    gl_Position = global.view_projection * vec4(World, 1.0);

#ifndef RENDERER_DEPTH
    vEyeVec = World - global.camera_position;
#endif

#ifndef RENDERER_DEPTH
#if HAVE_NORMAL
    #if HAVE_BONE_INDEX && HAVE_BONE_WEIGHT
        mat3 NormalTransform =
            mat3(BoneNormalTransforms[BoneIndices.x]) * BoneWeights.x +
            mat3(BoneNormalTransforms[BoneIndices.y]) * BoneWeights.y +
            mat3(BoneNormalTransforms[BoneIndices.z]) * BoneWeights.z +
            mat3(BoneNormalTransforms[BoneIndices.w]) * BoneWeights.w;
        vNormal = normalize(NormalTransform * Normal);
        #if HAVE_TANGENT
            vTangent = vec4(normalize(NormalTransform * Tangent.xyz), Tangent.w);
        #endif
    #else
        vNormal = normalize(mat3(infos[gl_InstanceIndex].Normal) * Normal);
        #if HAVE_TANGENT
            vTangent = vec4(normalize(mat3(infos[gl_InstanceIndex].Normal) * Tangent.xyz), Tangent.w);
        #endif
    #endif
#endif
#endif

#if HAVE_UV
    vUV = UV;
#endif

#if HAVE_VERTEX_COLOR
    vColor = VertexColor;
#endif
}
