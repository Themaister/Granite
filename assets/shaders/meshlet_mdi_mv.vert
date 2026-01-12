#version 450
#extension GL_ARB_shader_draw_parameters : require

invariant gl_Position;

#include "inc/meshlet_render_types.h"
#include "inc/global_bindings.h"
#include "inc/render_parameters.h"
#include "inc/affine.h"

layout(location = 0) in vec3 POS;
#if HAVE_BONE_INDEX && HAVE_BONE_WEIGHT
layout(location = 4) in uvec4 BONE_INDEX;
layout(location = 5) in vec4 BONE_WEIGHT;
#endif

layout(location = 0) out vec3 vOldClip;
layout(location = 1) out vec3 vNewClip;

layout(set = 0, binding = BINDING_GLOBAL_SCENE_NODE_TRANSFORMS) readonly buffer AffineTransforms
{
    mat_affine data[];
} transforms;

layout(set = 0, binding = BINDING_GLOBAL_SCENE_NODE_PREV_TRANSFORMS) readonly buffer PrevAffineTransforms
{
    mat_affine data[];
} prev_transforms;

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

    mat3x4 m_new = mat_affine_to_transposed(transforms.data[indices.x]) * BONE_WEIGHT.x;
    mat3x4 m_old = mat_affine_to_transposed(prev_transforms.data[indices.x]) * BONE_WEIGHT.x;

    if (BONE_WEIGHT.y != 0.0)
	{
        m_new += mat_affine_to_transposed(transforms.data[indices.y]) * BONE_WEIGHT.y;
        m_old += mat_affine_to_transposed(prev_transforms.data[indices.y]) * BONE_WEIGHT.y;
	}

    if (BONE_WEIGHT.z != 0.0)
	{
        m_new += mat_affine_to_transposed(transforms.data[indices.z]) * BONE_WEIGHT.z;
        m_old += mat_affine_to_transposed(prev_transforms.data[indices.z]) * BONE_WEIGHT.z;
	}

    if (BONE_WEIGHT.w != 0.0)
	{
        m_new += mat_affine_to_transposed(transforms.data[indices.w]) * BONE_WEIGHT.w;
        m_old += mat_affine_to_transposed(prev_transforms.data[indices.w]) * BONE_WEIGHT.w;
	}

    mat_affine M_new, M_old;
    M_new.rows[0] = m_new[0];
    M_new.rows[1] = m_new[1];
    M_new.rows[2] = m_new[2];

    M_old.rows[0] = m_old[0];
    M_old.rows[1] = m_old[1];
    M_old.rows[2] = m_old[2];
#else
    mat_affine M_new = transforms.data[node_offset];
    mat_affine M_old = prev_transforms.data[node_offset];
#endif

    vec3 world_pos = mul(M_new, POS);
    vec3 old_world_pos = mul(M_old, POS);

    gl_Position = global.view_projection * vec4(world_pos, 1.0);

	vOldClip = (global.unjittered_prev_view_projection * vec4(old_world_pos, 1.0)).xyw;
	vNewClip = (global.unjittered_view_projection * vec4(world_pos, 1.0)).xyw;
}
