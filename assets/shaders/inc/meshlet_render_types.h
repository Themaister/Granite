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
	uint node_count_material_index; // Skinning
	uint mesh_index_count;
};

struct CompactedDrawInfo
{
	uint meshlet_index;
	uint node_offset;
	uint node_count_material_offset;
};

struct IndirectDrawMesh
{
	uint primitive_offset;
	uint vertex_offset;
	uint num_primitives;
	uint num_attributes;
};

#if defined(MESHLET_RENDER_DRAW_WORDS) && MESHLET_RENDER_DRAW_WORDS
struct MeshletDrawCommand
{
	uint payload[MESHLET_RENDER_DRAW_WORDS];
};
#endif

#endif