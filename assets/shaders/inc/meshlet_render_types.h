#ifndef MESHLET_RENDER_TYPES_H_
#define MESHLET_RENDER_TYPES_H_

struct AABB
{
	vec3 lo; float pad0; vec3 hi; float pad;
};

struct Bound
{
	vec4 center_radius;
	vec4 cone;
};

struct MeshAssetDrawTaskInfo
{
	uint aabb_instance;
	uint occluder_state_offset;
	uint node_instance;
	uint mesh_index_count;
	uint material_flags;
};

const int MESH_ASSET_MATERIAL_TEXTURE_INDEX_OFFSET = 0;
const int MESH_ASSET_MATERIAL_TEXTURE_INDEX_BITS = 12;
const int MESH_ASSET_MATERIAL_PAYLOAD_OFFSET = 12;
const int MESH_ASSET_MATERIAL_PAYLOAD_BITS = 11;
const int MESH_ASSET_MATERIAL_UV_CLAMP_OFFSET = 23;
const int MESH_ASSET_MATERIAL_TEXTURE_MASK_OFFSET = 24;

const int MESH_ASSET_MATERIAL_BASE_COLOR_BIT = 1 << (MESH_ASSET_MATERIAL_TEXTURE_MASK_OFFSET + 0);
const int MESH_ASSET_MATERIAL_NORMAL_BIT = 1 << (MESH_ASSET_MATERIAL_TEXTURE_MASK_OFFSET + 1);
const int MESH_ASSET_MATERIAL_METALLIC_ROUGHNESS_BIT = 1 << (MESH_ASSET_MATERIAL_TEXTURE_MASK_OFFSET + 2);
const int MESH_ASSET_MATERIAL_OCCLUSION_BIT = 1 << (MESH_ASSET_MATERIAL_TEXTURE_MASK_OFFSET + 3);
const int MESH_ASSET_MATERIAL_EMISSIVE_BIT = 1 << (MESH_ASSET_MATERIAL_TEXTURE_MASK_OFFSET + 4);

struct CompactedDrawInfo
{
	uint meshlet_index;
	uint node_offset;
	uint material_flags;
};

#ifdef MESHLET_RENDER_TASK_HIERARCHICAL
#if MESHLET_RENDER_TASK_HIERARCHICAL
struct CompactedDrawInfoPayload
{
    uint task_offset_mesh_offsets[32 * 32];
};
#else
struct CompactedDrawInfoPayload
{
    CompactedDrawInfo info;
    uint8_t offsets[32];
};
#endif
#endif

#endif
