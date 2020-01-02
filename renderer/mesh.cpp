/* Copyright (c) 2017-2020 Hans-Kristian Arntzen
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "mesh.hpp"
#include "shader_suite.hpp"
#include "render_context.hpp"
#include "renderer.hpp"
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
	h.u32(topology);
	h.u32(primitive_restart);
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
	h.u64(material->get_hash());
	for (auto &attr : attributes)
	{
		h.u32(attr.format);
		h.u32(attr.offset);
	}
	return h.get();
}

Hash StaticMesh::get_baked_instance_key() const
{
	Hasher h;
	assert(cached_hash != 0);
	h.u64(cached_hash);
	h.u64(material->get_hash());
	return h.get();
}

namespace RenderFunctions
{
void mesh_set_state(CommandBuffer &cmd, const StaticMeshInfo &info)
{
	cmd.set_program(info.program);

	if (info.alpha_test)
		cmd.set_multisample_state(false, false, true);

	cmd.set_vertex_binding(0, *info.vbo_position, 0, info.position_stride);
	if (info.vbo_attributes)
		cmd.set_vertex_binding(1, *info.vbo_attributes, 0, info.attribute_stride);

	if (info.ibo)
		cmd.set_index_buffer(*info.ibo, 0, info.index_type);

	for (unsigned i = 0; i < ecast(MeshAttribute::Count); i++)
		if (info.attributes[i].format != VK_FORMAT_UNDEFINED)
			cmd.set_vertex_attrib(i, i == 0 ? 0 : 1, info.attributes[i].format, info.attributes[i].offset);

	auto &sampler = cmd.get_device().get_stock_sampler(info.sampler);
	for (unsigned i = 0; i < ecast(Material::Textures::Count); i++)
		if (info.views[i])
			cmd.set_texture(2, i, *info.views[i], sampler);

	cmd.push_constants(&info.fragment, 0, sizeof(info.fragment));
	cmd.set_primitive_topology(info.topology);
	cmd.set_primitive_restart(info.primitive_restart);

	if (info.two_sided)
		cmd.set_cull_mode(VK_CULL_MODE_NONE);
}

void debug_mesh_render(CommandBuffer &cmd, const RenderQueueData *infos, unsigned instances)
{
	auto *info = static_cast<const DebugMeshInfo *>(infos->render_info);

	cmd.set_program(info->program);
	cmd.push_constants(&info->MVP, 0, sizeof(info->MVP));
	cmd.set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
	cmd.set_vertex_attrib(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0);
	cmd.set_vertex_attrib(1, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0);

	unsigned count = 0;

	for (unsigned i = 0; i < instances; i++)
		count += static_cast<const DebugMeshInstanceInfo *>(infos[i].instance_data)->count;

	vec3 *pos = static_cast<vec3 *>(cmd.allocate_vertex_data(0, count * sizeof(vec3), sizeof(vec3)));
	vec4 *color = static_cast<vec4 *>(cmd.allocate_vertex_data(1, count * sizeof(vec4), sizeof(vec4)));

	count = 0;
	for (unsigned i = 0; i < instances; i++)
	{
		auto &draw = *static_cast<const DebugMeshInstanceInfo *>(infos[i].instance_data);
		memcpy(pos + count, draw.positions, draw.count * sizeof(vec3));
		memcpy(color + count, draw.colors, draw.count * sizeof(vec4));
		count += draw.count;
	}

	cmd.set_depth_bias(true);
	cmd.set_depth_bias(-1.0f, -1.0f);
	cmd.draw(count);
}

void static_mesh_render(CommandBuffer &cmd, const RenderQueueData *infos, unsigned instances)
{
	auto *info = static_cast<const StaticMeshInfo *>(infos->render_info);
	mesh_set_state(cmd, *info);

	unsigned to_render = 0;
	for (unsigned i = 0; i < instances; i += to_render)
	{
		to_render = min<unsigned>(StaticMeshVertex::max_instances, instances - i);

		auto *vertex_data = static_cast<StaticMeshVertex *>(cmd.allocate_constant_data(3, 0, to_render * sizeof(StaticMeshVertex)));
		for (unsigned j = 0; j < to_render; j++)
			vertex_data[j] = static_cast<const StaticMeshInstanceInfo *>(infos[i + j].instance_data)->vertex;

		if (info->ibo)
			cmd.draw_indexed(info->count, to_render, info->ibo_offset, info->vertex_offset, 0);
		else
			cmd.draw(info->count, to_render, info->vertex_offset, 0);
	}
}

void skinned_mesh_render(CommandBuffer &cmd, const RenderQueueData *infos, unsigned instances)
{
	auto *static_info = static_cast<const StaticMeshInfo *>(infos->render_info);
	mesh_set_state(cmd, *static_info);

	for (unsigned i = 0; i < instances; i++)
	{
		auto &info = *static_cast<const SkinnedMeshInstanceInfo *>(infos[i].instance_data);
		auto *world_transforms = static_cast<mat4 *>(cmd.allocate_constant_data(3, 1, sizeof(mat4) * info.num_bones));
		//auto *normal_transforms = static_cast<mat4 *>(cmd.allocate_constant_data(3, 2, sizeof(mat4) * info.num_bones));
		memcpy(world_transforms, info.world_transforms, sizeof(mat4) * info.num_bones);
		//memcpy(normal_transforms, info.normal_transforms, sizeof(mat4) * info.num_bones);

		if (static_info->ibo)
			cmd.draw_indexed(static_info->count, 1, static_info->ibo_offset, static_info->vertex_offset, 0);
		else
			cmd.draw(static_info->count, 1, static_info->vertex_offset, 0);
	}
}
}

void StaticMesh::fill_render_info(StaticMeshInfo &info) const
{
	info.vbo_attributes = vbo_attributes.get();
	info.vbo_position = vbo_position.get();
	info.position_stride = position_stride;
	info.attribute_stride = attribute_stride;
	info.vertex_offset = vertex_offset;

	info.ibo = ibo.get();
	info.ibo_offset = ibo_offset;
	info.index_type = index_type;
	info.count = count;
	info.sampler = material->sampler;

	info.fragment.roughness = material->roughness;
	info.fragment.metallic = material->metallic;
	info.fragment.emissive = vec4(material->emissive, 0.0f);
	info.fragment.base_color = material->base_color;
	info.fragment.normal_scale = material->normal_scale;

	info.topology = topology;
	info.primitive_restart = primitive_restart;
	info.two_sided = material->two_sided;
	info.alpha_test = material->pipeline == DrawPipeline::AlphaTest;

	memcpy(info.attributes, attributes, sizeof(attributes));
	for (unsigned i = 0; i < ecast(Material::Textures::Count); i++)
		info.views[i] = material->textures[i] ? &material->textures[i]->get_image()->get_view() : nullptr;
}

void StaticMesh::bake()
{
	cached_hash = get_instance_key();
}

static Queue material_to_queue(const Material &mat)
{
	if (mat.pipeline == DrawPipeline::AlphaBlend)
		return Queue::Transparent;
	else if (mat.needs_emissive)
		return Queue::OpaqueEmissive;
	else
		return Queue::Opaque;
}

void StaticMesh::get_render_info(const RenderContext &context, const RenderInfoComponent *transform, RenderQueue &queue) const
{
	auto type = material_to_queue(*material);
	uint32_t attrs = 0;

	for (unsigned i = 0; i < ecast(MeshAttribute::Count); i++)
		if (attributes[i].format != VK_FORMAT_UNDEFINED)
			attrs |= 1u << i;

	Hasher h;
	h.u32(attrs);
	h.u32(ecast(material->pipeline));
	h.u32(material->shader_variant);
	auto pipe_hash = h.get();

	h.u64(material->get_hash());
	h.u64(vbo_position->get_cookie());

	auto instance_key = get_baked_instance_key();
	auto sorting_key = RenderInfo::get_sort_key(context, type, pipe_hash, h.get(), transform->world_aabb.get_center());

	auto *t = transform->transform;
	auto *instance_data = queue.allocate_one<StaticMeshInstanceInfo>();
	instance_data->vertex.Model = t->world_transform;
	//instance_data->vertex.Normal = t->normal_transform;

	auto *mesh_info = queue.push<StaticMeshInfo>(type, instance_key, sorting_key,
	                                             RenderFunctions::static_mesh_render,
	                                             instance_data);

	if (mesh_info)
	{
		uint32_t textures = 0;
		for (unsigned i = 0; i < ecast(Material::Textures::Count); i++)
			if (material->textures[i])
				textures |= 1u << i;

		if (type == Queue::OpaqueEmissive)
			textures |= MATERIAL_EMISSIVE_BIT;

		fill_render_info(*mesh_info);
		mesh_info->program = queue.get_shader_suites()[ecast(RenderableType::Mesh)].get_program(material->pipeline, attrs,
		                                                                                        textures, material->shader_variant);
	}
}

void SkinnedMesh::get_render_info(const RenderContext &context, const RenderInfoComponent *transform, RenderQueue &queue) const
{
	auto type = material_to_queue(*material);
	uint32_t attrs = 0;
	uint32_t textures = 0;

	for (unsigned i = 0; i < ecast(MeshAttribute::Count); i++)
		if (attributes[i].format != VK_FORMAT_UNDEFINED)
			attrs |= 1u << i;

	for (unsigned i = 0; i < ecast(Material::Textures::Count); i++)
		if (material->textures[i])
			textures |= 1u << i;

	Hasher h;
	h.u32(attrs);
	h.u32(textures);
	h.u32(ecast(material->pipeline));
	h.u32(material->shader_variant);
	auto pipe_hash = h.get();

	h.u64(material->get_hash());
	h.u64(vbo_position->get_cookie());

	auto instance_key = get_baked_instance_key() ^ 1;
	auto sorting_key = RenderInfo::get_sort_key(context, type, pipe_hash, h.get(), transform->world_aabb.get_center());

	auto *instance_data = queue.allocate_one<SkinnedMeshInstanceInfo>();

	unsigned num_bones = transform->skin_transform->bone_world_transforms.size();
	instance_data->num_bones = num_bones;
	instance_data->world_transforms = queue.allocate_many<mat4>(num_bones);
	//instance_data->normal_transforms = queue.allocate_many<mat4>(num_bones);
	memcpy(instance_data->world_transforms, transform->skin_transform->bone_world_transforms.data(), num_bones * sizeof(mat4));
	//memcpy(instance_data->normal_transforms, transform->skin_transform->bone_normal_transforms.data(), num_bones * sizeof(mat4));

	auto *mesh_info = queue.push<StaticMeshInfo>(type, instance_key, sorting_key,
	                                             RenderFunctions::skinned_mesh_render,
	                                             instance_data);

	if (mesh_info)
	{
		fill_render_info(*mesh_info);
		mesh_info->program = queue.get_shader_suites()[ecast(RenderableType::Mesh)].get_program(material->pipeline, attrs,
		                                                                                        textures, material->shader_variant);
	}
}

void StaticMesh::reset()
{
	vbo_attributes.reset();
	vbo_position.reset();
	ibo.reset();
	material.reset();
}
}
