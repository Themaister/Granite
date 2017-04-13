#include "mesh.hpp"
#include "shader_suite.hpp"
#include "render_context.hpp"
#include <string.h>

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
		cmd.set_vertex_binding(1, *info->vbo_attributes, 0, info->attribute_stride);

	if (info->ibo)
		cmd.set_index_buffer(*info->ibo, 0, info->index_type);

	for (unsigned i = 0; i < ecast(MeshAttribute::Count); i++)
		if (info->attributes[i].format != VK_FORMAT_UNDEFINED)
			cmd.set_vertex_attrib(i, i == 0 ? 0 : 1, info->attributes[i].format, info->attributes[i].offset);

	for (unsigned i = 0; i < ecast(Material::Textures::Count); i++)
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

void StaticMesh::get_render_info(const RenderContext &context, const CachedSpatialTransformComponent *transform, RenderQueue &queue) const
{
	auto type = pipeline == MeshDrawPipeline::AlphaBlend ? Queue::Transparent : Queue::Opaque;
	auto &info = queue.emplace<StaticMeshInfo>(type);

	info.render = RenderFunctions::static_mesh_render;
	info.vbo_attributes = vbo_attributes.get();
	info.vbo_position = vbo_position.get();
	info.position_stride = position_stride;
	info.attribute_stride = attribute_stride;
	info.vertex_offset = vertex_offset;

	info.ibo = ibo.get();
	info.ibo_offset = ibo_offset;
	info.index_type = index_type;
	info.count = count;
	info.sampler = &context.get_device().get_stock_sampler(StockSampler::TrilinearClamp);

	info.vertex.Normal = transform->normal_transform;
	info.vertex.MVP = context.get_render_parameters().view_projection * transform->world_transform;
	info.fragment.roughness = material->roughness;
	info.fragment.metallic = material->metallic;
	info.fragment.emissive = material->emissive;
	info.fragment.albedo = material->albedo;

	info.instance_key = get_instance_key();

	uint32_t attrs = 0;
	uint32_t textures = 0;

	for (unsigned i = 0; i < ecast(MeshAttribute::Count); i++)
		if (attributes[i].format != VK_FORMAT_UNDEFINED)
			attrs |= 1u << i;
	memcpy(info.attributes, attributes, sizeof(attributes));

	for (unsigned i = 0; i < ecast(Material::Textures::Count); i++)
	{
		info.views[i] = material->textures[i] ? &material->textures[i]->get_image()->get_view() : nullptr;
		if (material->textures[i])
			textures |= 1u << i;
	}

	info.program = queue.get_shader_suite()->get_program(pipeline, attrs, textures).get();
	Hasher h;
	h.pointer(info.program);
	h.u32(ecast(pipeline));
	h.u32(attrs);
	h.u32(textures);
	info.sorting_key = RenderInfo::get_sort_key(context, type, h.get(), transform->world_aabb.get_center());
}

void StaticMesh::reset()
{
	vbo_attributes.reset();
	vbo_position.reset();
	ibo.reset();
	material.reset();
}
}