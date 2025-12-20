#version 450

#if defined(MULTIVIEW) && MULTIVIEW
#extension GL_EXT_multiview : require
#endif

#include "inc/render_parameters.h"
#include "inc/affine.h"

layout(location = 0) in highp vec3 Position;

#if defined(RENDERER_DEPTH)
    #if HAVE_UV && defined(ALPHA_TEST)
        layout(location = 1) in highp vec2 UV;
        layout(location = 1) out highp vec2 vUV;
    #endif
#elif !defined(RENDERER_MOTION_VECTOR)
    #if HAVE_UV
        layout(location = 1) in highp vec2 UV;
        layout(location = 1) out highp vec2 vUV;
    #endif
#endif

#if !defined(RENDERER_DEPTH) && !defined(RENDERER_MOTION_VECTOR)
    layout(location = 0) out highp vec3 vPos;

    #if HAVE_NORMAL
    layout(location = 2) in mediump vec3 Normal;
    layout(location = 2) out mediump vec3 vNormal;
    #endif

    #if HAVE_TANGENT
    layout(location = 3) in mediump vec4 Tangent;
    layout(location = 3) out mediump vec4 vTangent;
    #endif

    #if HAVE_VERTEX_COLOR
    layout(location = 6) in mediump vec4 VertexColor;
    layout(location = 4) out mediump vec4 vColor;
    #endif
#endif

#if defined(RENDERER_MOTION_VECTOR)
    layout(location = 0) out highp vec3 vOldClip;
    layout(location = 1) out highp vec3 vNewClip;
#endif

#if HAVE_BONE_INDEX
layout(location = 4) in mediump uvec4 BoneIndices;
#endif

#if HAVE_BONE_WEIGHT
layout(location = 5) in mediump vec4 BoneWeights;
#endif

#if HAVE_BONE_INDEX && HAVE_BONE_WEIGHT
layout(std140, set = 3, binding = 1) uniform BonesWorld
{
    mat_affine CurrentBoneWorldTransforms[256];
};

#if defined(RENDERER_MOTION_VECTOR)
layout(std140, set = 3, binding = 3) uniform BonesWorldPrev
{
    mat_affine PrevBoneWorldTransforms[256];
};
#endif
#else
struct StaticMeshInfo
{
    mat_affine Model;
};

layout(set = 3, binding = 0, std140) uniform PerVertexData
{
    StaticMeshInfo CurrentInfos[256];
};

#if defined(RENDERER_MOTION_VECTOR)
layout(set = 3, binding = 2, std140) uniform PerVertexDataPrev
{
    StaticMeshInfo PrevInfos[256];
};
#endif
#endif

invariant gl_Position;

#if HAVE_BONE_INDEX && HAVE_BONE_WEIGHT
#define MODEL_VIEW_TRANSFORM(prefix) \
    mat_affine_to_transposed(prefix##BoneWorldTransforms[BoneIndices.x]) * BoneWeights.x + \
    mat_affine_to_transposed(prefix##BoneWorldTransforms[BoneIndices.y]) * BoneWeights.y + \
    mat_affine_to_transposed(prefix##BoneWorldTransforms[BoneIndices.z]) * BoneWeights.z + \
    mat_affine_to_transposed(prefix##BoneWorldTransforms[BoneIndices.w]) * BoneWeights.w
#else
#define MODEL_VIEW_TRANSFORM(prefix) \
    mat_affine_to_transposed(prefix##Infos[gl_InstanceIndex].Model)
#endif

void main()
{
    mat3x4 WorldTransform = MODEL_VIEW_TRANSFORM(Current);
    vec3 World = vec4(Position, 1.0) * WorldTransform;

#if defined(RENDERER_MOTION_VECTOR)
    vec3 OldWorld = vec4(Position, 1.0) * MODEL_VIEW_TRANSFORM(Prev);
    vOldClip = (global.unjittered_prev_view_projection * vec4(OldWorld, 1.0)).xyw;
    vNewClip = (global.unjittered_view_projection * vec4(World, 1.0)).xyw;
#endif

#if defined(MULTIVIEW) && MULTIVIEW
    gl_Position = global.multiview_view_projection[gl_ViewIndex] * vec4(World, 1.0);
#else
    gl_Position = global.view_projection * vec4(World, 1.0);
#endif

#if !defined(RENDERER_DEPTH) && !defined(RENDERER_MOTION_VECTOR)
    vPos = World;
    #if HAVE_NORMAL
        mat3 NormalTransform = mat3(WorldTransform[0].xyz, WorldTransform[1].xyz, WorldTransform[2].xyz);
        #if HAVE_BONE_INDEX && HAVE_BONE_WEIGHT
            vNormal = normalize(Normal * NormalTransform);
            #if HAVE_TANGENT
                vTangent = vec4(normalize(Tangent.xyz * NormalTransform), Tangent.w);
            #endif
        #else
            vNormal = normalize(Normal * NormalTransform);
            #if HAVE_TANGENT
                vTangent = vec4(normalize(Tangent.xyz * NormalTransform), Tangent.w);
            #endif
        #endif
    #endif

    #if HAVE_VERTEX_COLOR
        vColor = VertexColor;
    #endif
#endif

#if defined(RENDERER_DEPTH)
    #if HAVE_UV && defined(ALPHA_TEST)
        vUV = UV;
    #endif
#elif !defined(RENDERER_MOTION_VECTOR)
    #if HAVE_UV
        vUV = UV;
    #endif
#endif
}
