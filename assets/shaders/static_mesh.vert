#version 450

#if defined(MULTIVIEW) && MULTIVIEW
#extension GL_EXT_multiview : require
#endif

#include "inc/render_parameters.h"

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
    mat4 CurrentBoneWorldTransforms[256];
};

#if defined(RENDERER_MOTION_VECTOR)
layout(std140, set = 3, binding = 3) uniform BonesWorldPrev
{
    mat4 PrevBoneWorldTransforms[256];
};
#endif
#else
struct StaticMeshInfo
{
    mat4 Model;
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
    mat4x3( \
        prefix##BoneWorldTransforms[BoneIndices.x][0].xyz * BoneWeights.x + \
        prefix##BoneWorldTransforms[BoneIndices.y][0].xyz * BoneWeights.y + \
        prefix##BoneWorldTransforms[BoneIndices.z][0].xyz * BoneWeights.z + \
        prefix##BoneWorldTransforms[BoneIndices.w][0].xyz * BoneWeights.w, \
        prefix##BoneWorldTransforms[BoneIndices.x][1].xyz * BoneWeights.x + \
        prefix##BoneWorldTransforms[BoneIndices.y][1].xyz * BoneWeights.y + \
        prefix##BoneWorldTransforms[BoneIndices.z][1].xyz * BoneWeights.z + \
        prefix##BoneWorldTransforms[BoneIndices.w][1].xyz * BoneWeights.w, \
        prefix##BoneWorldTransforms[BoneIndices.x][2].xyz * BoneWeights.x + \
        prefix##BoneWorldTransforms[BoneIndices.y][2].xyz * BoneWeights.y + \
        prefix##BoneWorldTransforms[BoneIndices.z][2].xyz * BoneWeights.z + \
        prefix##BoneWorldTransforms[BoneIndices.w][2].xyz * BoneWeights.w, \
        prefix##BoneWorldTransforms[BoneIndices.x][3].xyz * BoneWeights.x + \
        prefix##BoneWorldTransforms[BoneIndices.y][3].xyz * BoneWeights.y + \
        prefix##BoneWorldTransforms[BoneIndices.z][3].xyz * BoneWeights.z + \
        prefix##BoneWorldTransforms[BoneIndices.w][3].xyz * BoneWeights.w)
#else
#define MODEL_VIEW_TRANSFORM(prefix) \
    mat4x3( \
        prefix##Infos[gl_InstanceIndex].Model[0].xyz, \
        prefix##Infos[gl_InstanceIndex].Model[1].xyz, \
        prefix##Infos[gl_InstanceIndex].Model[2].xyz, \
        prefix##Infos[gl_InstanceIndex].Model[3].xyz)
#endif

void main()
{
    mat4x3 WorldTransform = MODEL_VIEW_TRANSFORM(Current);
    vec3 World = WorldTransform * vec4(Position, 1.0);

#if defined(RENDERER_MOTION_VECTOR)
    vec3 OldWorld = MODEL_VIEW_TRANSFORM(Prev) * vec4(Position, 1.0);
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
        mat3 NormalTransform = mat3(WorldTransform[0], WorldTransform[1], WorldTransform[2]);
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
