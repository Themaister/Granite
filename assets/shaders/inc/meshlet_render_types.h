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

struct TaskInfo
{
	uint aabb_instance;
	uint node_instance;
	uint material_index;
	uint mesh_index_count;
	uint occluder_state_offset;
};

struct CompactedDrawInfo
{
	uint meshlet_index;
	uint node_offset;
	uint material_offset;
};

#ifdef MESHLET_RENDER_TASK_HIERARCHICAL
#if MESHLET_RENDER_TASK_HIERARCHICAL
struct CompactedDrawInfoPayload
{
    CompactedDrawInfo infos[32 * 32];
};
#else
struct CompactedDrawInfoPayload
{
    CompactedDrawInfo info;
    uint8_t offsets[32];
};
#endif
#endif

struct IndirectDrawMesh
{
	uint primitive_offset;
	uint vertex_offset;
	uint primitive_count;
	uint vertex_count;
};

#endif
