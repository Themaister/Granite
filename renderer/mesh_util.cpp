/* Copyright (c) 2017 Hans-Kristian Arntzen
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

#include "mesh_util.hpp"
#include "device.hpp"
#include "material_util.hpp"
#include "material_manager.hpp"
#include "render_context.hpp"
#include "shader_suite.hpp"
#include "renderer.hpp"
#include "application.hpp"
#include <string.h>

using namespace Vulkan;
using namespace Util;
using namespace Granite::Importer;

namespace Granite
{
ImportedSkinnedMesh::ImportedSkinnedMesh(const Mesh &mesh, const MaterialInfo &info)
	: mesh(mesh), info(info)
{
	topology = mesh.topology;
	index_type = mesh.index_type;

	position_stride = mesh.position_stride;
	attribute_stride = mesh.attribute_stride;
	memcpy(attributes, mesh.attribute_layout, sizeof(mesh.attribute_layout));

	count = mesh.count;
	vertex_offset = 0;
	ibo_offset = 0;

	material = Util::make_abstract_handle<Material, MaterialFile>(info);
	static_aabb = mesh.static_aabb;

	EVENT_MANAGER_REGISTER_LATCH(ImportedSkinnedMesh, on_device_created, on_device_destroyed, DeviceCreatedEvent);
}

void ImportedSkinnedMesh::on_device_created(const Event &event)
{
	auto &created = event.as<DeviceCreatedEvent>();
	auto &device = created.get_device();

	BufferCreateInfo info = {};
	info.domain = BufferDomain::Device;
	info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

	info.size = mesh.positions.size();
	vbo_position = device.create_buffer(info, mesh.positions.data());

	if (!mesh.attributes.empty())
	{
		info.size = mesh.attributes.size();
		vbo_attributes = device.create_buffer(info, mesh.attributes.data());
	}

	if (!mesh.indices.empty())
	{
		info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
		info.size = mesh.indices.size();
		ibo = device.create_buffer(info, mesh.indices.data());
	}

	bake();
}

void ImportedSkinnedMesh::on_device_destroyed(const Event &)
{
	vbo_attributes.reset();
	vbo_position.reset();
	ibo.reset();
}

ImportedMesh::ImportedMesh(const Mesh &mesh, const MaterialInfo &info)
	: mesh(mesh), info(info)
{
	topology = mesh.topology;
	index_type = mesh.index_type;

	position_stride = mesh.position_stride;
	attribute_stride = mesh.attribute_stride;
	memcpy(attributes, mesh.attribute_layout, sizeof(mesh.attribute_layout));

	count = mesh.count;
	vertex_offset = 0;
	ibo_offset = 0;

	material = Util::make_abstract_handle<Material, MaterialFile>(info);
	static_aabb = mesh.static_aabb;

	EVENT_MANAGER_REGISTER_LATCH(ImportedMesh, on_device_created, on_device_destroyed, DeviceCreatedEvent);
}

void ImportedMesh::on_device_created(const Event &event)
{
	auto &created = event.as<DeviceCreatedEvent>();
	auto &device = created.get_device();

	BufferCreateInfo info = {};
	info.domain = BufferDomain::Device;
	info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

	info.size = mesh.positions.size();
	vbo_position = device.create_buffer(info, mesh.positions.data());

	if (!mesh.attributes.empty())
	{
		info.size = mesh.attributes.size();
		vbo_attributes = device.create_buffer(info, mesh.attributes.data());
	}

	if (!mesh.indices.empty())
	{
		info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
		info.size = mesh.indices.size();
		ibo = device.create_buffer(info, mesh.indices.data());
	}

	bake();
}

void ImportedMesh::on_device_destroyed(const Event &)
{
	vbo_attributes.reset();
	vbo_position.reset();
	ibo.reset();
}

CubeMesh::CubeMesh()
{
	static_aabb = AABB(vec3(-1.0f), vec3(1.0f));
	EVENT_MANAGER_REGISTER_LATCH(CubeMesh, on_device_created, on_device_destroyed, DeviceCreatedEvent);
}

void CubeMesh::on_device_created(const Event &event)
{
	auto &created = event.as<DeviceCreatedEvent>();
	auto &device = created.get_device();

	static const int8_t N = -128;
	static const int8_t P = +127;

	static const int8_t positions[] = {
		// Near
		N, N, P, P,
		P, N, P, P,
		N, P, P, P,
		P, P, P, P,

		// Far
		P, N, N, P,
		N, N, N, P,
		P, P, N, P,
		N, P, N, P,

	    // Left
		N, N, N, P,
		N, N, P, P,
		N, P, N, P,
		N, P, P, P,

	    // Right
		P, N, P, P,
		P, N, N, P,
		P, P, P, P,
		P, P, N, P,

	    // Top
		N, P, P, P,
		P, P, P, P,
		N, P, N, P,
		P, P, N, P,

	    // Bottom
		N, N, N, P,
		P, N, N, P,
		N, N, P, P,
		P, N, P, P,
	};

	static const int8_t attr[] = {
		// Near
		0, 0, P, 0, P, 0, 0, 0, 0, P,
		0, 0, P, 0, P, 0, 0, 0, P, P,
		0, 0, P, 0, P, 0, 0, 0, 0, 0,
		0, 0, P, 0, P, 0, 0, 0, P, 0,

	    // Far
		0, 0, N, 0, N, 0, 0, 0, 0, P,
		0, 0, N, 0, N, 0, 0, 0, P, P,
		0, 0, N, 0, N, 0, 0, 0, 0, 0,
		0, 0, N, 0, N, 0, 0, 0, P, 0,

	    // Left
		N, 0, 0, 0, 0, 0, P, 0, 0, P,
		N, 0, 0, 0, 0, 0, P, 0, P, P,
		N, 0, 0, 0, 0, 0, P, 0, 0, 0,
		N, 0, 0, 0, 0, 0, P, 0, P, 0,

		// Right
		P, 0, 0, 0, 0, 0, N, 0, 0, P,
		P, 0, 0, 0, 0, 0, N, 0, P, P,
		P, 0, 0, 0, 0, 0, N, 0, 0, 0,
		P, 0, 0, 0, 0, 0, N, 0, P, 0,

		// Top
		0, P, 0, 0, P, 0, 0, 0, 0, P,
		0, P, 0, 0, P, 0, 0, 0, P, P,
		0, P, 0, 0, P, 0, 0, 0, 0, 0,
		0, P, 0, 0, P, 0, 0, 0, P, 0,

		// Bottom
		0, N, 0, 0, P, 0, 0, 0, 0, P,
		0, N, 0, 0, P, 0, 0, 0, P, P,
		0, N, 0, 0, P, 0, 0, 0, 0, 0,
		0, N, 0, 0, P, 0, 0, 0, P, 0,
	};

	BufferCreateInfo vbo_info = {};
	vbo_info.domain = BufferDomain::Device;
	vbo_info.size = sizeof(positions);
	vbo_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	vbo_position = device.create_buffer(vbo_info, positions);
	position_stride = 4;

	attributes[ecast(MeshAttribute::Position)].offset = 0;
	attributes[ecast(MeshAttribute::Position)].format = VK_FORMAT_R8G8B8A8_SNORM;

	attributes[ecast(MeshAttribute::Normal)].offset = 0;
	attributes[ecast(MeshAttribute::Normal)].format = VK_FORMAT_R8G8B8A8_SNORM;
	attributes[ecast(MeshAttribute::Tangent)].offset = 4;
	attributes[ecast(MeshAttribute::Tangent)].format = VK_FORMAT_R8G8B8A8_SNORM;
	attributes[ecast(MeshAttribute::UV)].offset = 8;
	attributes[ecast(MeshAttribute::UV)].format = VK_FORMAT_R8G8_SNORM;
	attribute_stride = 10;

	vbo_info.size = sizeof(attr);
	vbo_attributes = device.create_buffer(vbo_info, attr);

	static const uint16_t indices[] = {
		0, 1, 2, 3, 2, 1,
		4, 5, 6, 7, 6, 5,
		8, 9, 10, 11, 10, 9,
		12, 13, 14, 15, 14, 13,
		16, 17, 18, 19, 18, 17,
		20, 21, 22, 23, 22, 21,
	};
	BufferCreateInfo ibo_info = {};
	ibo_info.size = sizeof(indices);
	ibo_info.domain = BufferDomain::Device;
	ibo_info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	ibo = device.create_buffer(ibo_info, indices);
	//material = StockMaterials::get().get_checkerboard();
	material = MaterialManager::get().request_material("builtin://materials/default.json");

	vertex_offset = 0;
	ibo_offset = 0;
	count = 36;
	bake();
}

void CubeMesh::on_device_destroyed(const Event &)
{
	reset();
}

Skybox::Skybox(std::string bg_path)
	: bg_path(move(bg_path))
{
	EVENT_MANAGER_REGISTER_LATCH(Skybox, on_device_created, on_device_destroyed, DeviceCreatedEvent);
}

struct SkyboxRenderInfo
{
	Program *program;
	const ImageView *view;
	const Sampler *sampler;
};

static void skybox_render(CommandBuffer &cmd, const RenderQueueData *infos, unsigned instances)
{
	for (unsigned i = 0; i < instances; i++)
	{
		auto *info = static_cast<const SkyboxRenderInfo *>(infos[i].render_info);

		cmd.set_program(*info->program);
		cmd.set_texture(2, 0, *info->view, *info->sampler);

		CommandBufferUtil::set_quad_vertex_state(cmd);
		cmd.set_cull_mode(VK_CULL_MODE_NONE);
		cmd.set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
		cmd.draw(4);
	}
}

void Skybox::get_render_info(const RenderContext &context, const CachedSpatialTransformComponent *,
                             RenderQueue &queue) const
{
	SkyboxRenderInfo info;
	info.view = &texture->get_image()->get_view();

	Hasher h;
	h.pointer(info.view);
	auto instance_key = h.get();
	auto sorting_key = RenderInfo::get_background_sort_key(Queue::Opaque, 0, 0);
	info.sampler = &context.get_device().get_stock_sampler(StockSampler::LinearClamp);

	auto *skydome_info = queue.push<SkyboxRenderInfo>(Queue::Opaque, instance_key, sorting_key,
	                                                  skybox_render,
	                                                  nullptr);

	if (skydome_info)
	{
		info.program = queue.get_shader_suites()[ecast(RenderableType::Skybox)].get_program(DrawPipeline::Opaque, 0, 0).get();
		*skydome_info = info;
	}
}

void Skybox::on_device_created(const Event &event)
{
	texture = event.as<DeviceCreatedEvent>().get_device().get_texture_manager().request_texture(bg_path);
}

void Skybox::on_device_destroyed(const Event &)
{
	texture = nullptr;
}

struct TexturePlaneInfo
{
	Vulkan::Program *program;
	const Vulkan::ImageView *reflection;
	const Vulkan::ImageView *refraction;
	const Vulkan::ImageView *normal;

	struct Push
	{
		vec4 normal;
		vec4 tangent;
		vec4 bitangent;
		vec4 position;
		vec4 dPdx;
		vec4 dPdy;
		vec4 offset_scale;
	};
	Push push;
};

static void texture_plane_render(CommandBuffer &cmd, const RenderQueueData *infos, unsigned instances)
{
	for (unsigned i = 0; i < instances; i++)
	{
		auto &info = *static_cast<const TexturePlaneInfo *>(infos[i].render_info);
		cmd.set_program(*info.program);
		cmd.set_texture(2, 0, *info.reflection, Vulkan::StockSampler::TrilinearClamp);
		cmd.set_texture(2, 1, *info.refraction, Vulkan::StockSampler::TrilinearClamp);
		cmd.set_texture(2, 2, *info.normal, Vulkan::StockSampler::TrilinearWrap);
		CommandBufferUtil::set_quad_vertex_state(cmd);
		cmd.set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
		cmd.set_cull_mode(VK_CULL_MODE_NONE);
		cmd.push_constants(&info.push, 0, sizeof(info.push));
		cmd.draw(4);
	}
}

TexturePlane::TexturePlane(const std::string &normal)
	: normal_path(normal)
{
	EVENT_MANAGER_REGISTER_LATCH(TexturePlane, on_device_created, on_device_destroyed, DeviceCreatedEvent);
	EVENT_MANAGER_REGISTER(TexturePlane, on_frame_time, FrameTickEvent);
}

bool TexturePlane::on_frame_time(const Event &e)
{
	elapsed = e.as<FrameTickEvent>().get_elapsed_time();
	return true;
}

void TexturePlane::on_device_created(const Event &event)
{
	normalmap = event.as<DeviceCreatedEvent>().get_device().get_texture_manager().request_texture(normal_path);
}

void TexturePlane::on_device_destroyed(const Event &)
{
	normalmap = nullptr;
}

void TexturePlane::get_render_info(const RenderContext &context, const CachedSpatialTransformComponent *,
                                   RenderQueue &queue) const
{
	TexturePlaneInfo info;
	info.reflection = reflection;
	info.refraction = refraction;
	info.normal = &normalmap->get_image()->get_view();
	info.push.normal = vec4(normalize(normal), 0.0f);
	info.push.position = vec4(position, 0.0f);
	info.push.dPdx = vec4(dpdx, 0.0f);
	info.push.dPdy = vec4(dpdy, 0.0f);
	info.push.tangent = vec4(normalize(dpdx), 0.0f);
	info.push.bitangent = vec4(normalize(dpdy), 0.0f);
	info.push.offset_scale = vec4(vec2(0.03 * elapsed), vec2(2.0f));

	Hasher h;
	h.u64(info.reflection->get_cookie());
	h.u64(info.refraction->get_cookie());
	h.u64(info.normal->get_cookie());
	auto instance_key = h.get();
	auto sorting_key = RenderInfo::get_sort_key(context, Queue::OpaqueEmissive, h.get(), h.get(), position);
	auto *plane_info = queue.push<TexturePlaneInfo>(Queue::OpaqueEmissive, instance_key, sorting_key,
	                                                texture_plane_render, nullptr);

	if (plane_info)
	{
		info.program = queue.get_shader_suites()[ecast(RenderableType::TexturePlane)].get_program(DrawPipeline::Opaque, 0, MATERIAL_EMISSIVE_BIT).get();
		*plane_info = info;
	}
}

}
