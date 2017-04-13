#include "mesh.hpp"

using namespace Util;
using namespace Vulkan;

namespace Granite
{
Hash StaticMesh::get_instance_key() const
{
	Hasher h;
	h.u64(vbo_position->get_cookie());
	h.u32(position_stride);
	if (vbo_attributes)
	{
		h.u64(vbo_attributes->get_cookie());
		h.u32(attribute_stride);
	}
	if (ibo)
	{
		h.u64(ibo->get_cookie());
		h.u32(ibo_offset);
		h.u32(index_type);
	}
	h.u32(count);
	h.u32(vertex_offset);
	h.u32(position_stride);
	h.pointer(material.get());
	for (auto &attr : attributes)
	{
		h.u32(attr.format);
		h.u32(attr.offset);
	}
	return h.get();
}

namespace RenderFunctions
{

void static_mesh_render(CommandBuffer &cmd, const RenderInfo **infos, unsigned instances)
{
	auto *info = static_cast<const StaticMeshInfo *>(infos[0]);
	cmd.set_program(*info->program);

	cmd.set_vertex_binding(0, *info->vbo_position, 0, info->position_stride);
	if (info->vbo_attributes)
		cmd.set_vertex_binding(1, *info->vbo_attributes, 0, info->position_stride);

	if (info->ibo)
		cmd.set_index_buffer(*info->ibo, 0, info->index_type);

	for (unsigned i = 0; i < static_cast<unsigned>(MeshAttribute::Count); i++)
		if (info->attributes[i].format != VK_FORMAT_UNDEFINED)
			cmd.set_vertex_attrib(0, i == 0 ? 0 : 1, info->attributes[i].format, info->attributes[i].offset);

	for (unsigned i = 0; i < static_cast<unsigned>(Material::Textures::Count); i++)
		if (info->views[i])
			cmd.set_texture(2, i, *info->views[i], *info->sampler);

	cmd.push_constants(&info->fragment, 0, sizeof(info->fragment));

	unsigned to_render = 0;
	for (unsigned i = 0; i < instances; i += to_render)
	{
		to_render = min<unsigned>(StaticMeshVertex::max_instances, instances - i);

		auto *vertex_data = static_cast<StaticMeshVertex *>(cmd.allocate_constant_data(3, 0, to_render * sizeof(StaticMeshVertex)));
		for (unsigned j = 0; j < to_render; j++)
			vertex_data[j] = static_cast<const StaticMeshInfo *>(infos[i + j])->vertex;

		if (info->ibo)
		{
			cmd.set_primitive_restart(true);
			cmd.draw_indexed(info->count, to_render, info->ibo_offset, info->vertex_offset, 0);
		}
		else
			cmd.draw(info->count, to_render, info->vertex_offset, 0);
	}
}
}

void StaticMesh::get_render_info(const RenderContext &, const CachedSpatialTransformComponent *, RenderQueue &) const
{
}

void StaticMesh::reset()
{
	vbo_attributes.reset();
	vbo_position.reset();
	ibo.reset();
	material.reset();
}
}