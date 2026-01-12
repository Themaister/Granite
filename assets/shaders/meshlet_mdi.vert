#version 450
#extension GL_ARB_shader_draw_parameters : require

invariant gl_Position;

#include "inc/meshlet_render_types.h"
#include "inc/global_bindings.h"
#include "inc/render_parameters.h"
#include "inc/affine.h"

#if !defined(RENDERER_DEPTH)
#define ATTR_LEVEL 2
#elif defined(ALPHA_TEST)
#define ATTR_LEVEL 1
#else
#define ATTR_LEVEL 0
#endif

layout(location = 0) in vec3 POS;
layout(location = 1) in mediump vec3 NORMAL;
layout(location = 2) in mediump vec4 TANGENT;
layout(location = 3) in vec2 UV;
#if HAVE_BONE_INDEX && HAVE_BONE_WEIGHT
layout(location = 4) in uvec4 BONE_INDEX;
layout(location = 5) in vec4 BONE_WEIGHT;
#endif

#if ATTR_LEVEL >= 1
layout(location = 0) out vec2 vUV;
layout(location = 1) out uint vMaterialFlags;
#endif

#if ATTR_LEVEL >= 2
layout(location = 2) out mediump vec3 vNormal;
layout(location = 3) out mediump vec4 vTangent;
layout(location = 4) out vec3 vPos;
#endif

layout(set = 0, binding = BINDING_GLOBAL_SCENE_NODE_TRANSFORMS) readonly buffer AffineTransforms
{
    mat_affine data[];
} transforms;

layout(set = 0, binding = BINDING_GLOBAL_SCENE_TASK_BUFFER, std430) readonly buffer Tasks
{
    MeshAssetDrawTaskInfo data[];
} task_info;

void main()
{
    uint task_offset = gl_BaseInstanceARB;
    uint node_offset = task_info.data[task_offset].node_instance;

#if HAVE_BONE_INDEX && HAVE_BONE_WEIGHT
    uvec4 indices = BONE_INDEX + node_offset;

    mat3x4 m = mat_affine_to_transposed(transforms.data[indices.x]) * BONE_WEIGHT.x;
    if (BONE_WEIGHT.y != 0.0)
        m += mat_affine_to_transposed(transforms.data[indices.y]) * BONE_WEIGHT.y;
    if (BONE_WEIGHT.z != 0.0)
        m += mat_affine_to_transposed(transforms.data[indices.z]) * BONE_WEIGHT.z;
    if (BONE_WEIGHT.w != 0.0)
        m += mat_affine_to_transposed(transforms.data[indices.w]) * BONE_WEIGHT.w;

    mat_affine M;
    M.rows[0] = m[0];
    M.rows[1] = m[1];
    M.rows[2] = m[2];
#else
    mat_affine M = transforms.data[node_offset];
#endif

    vec3 world_pos = mul(M, POS);
    gl_Position = global.view_projection * vec4(world_pos, 1.0);

#if ATTR_LEVEL >= 1
    vMaterialFlags = task_info.data[task_offset].material_flags;
    vUV = UV;
#endif

#if ATTR_LEVEL >= 2
    vNormal = mul_normal(M, NORMAL);
    vTangent = vec4(mul_normal(M, TANGENT.xyz), TANGENT.w);
    vPos = world_pos;
#endif
}
