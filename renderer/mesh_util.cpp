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

	EventManager::get_global().register_latch_handler(DeviceCreatedEvent::type_id,
	                                                  &ImportedSkinnedMesh::on_device_created,
	                                                  &ImportedSkinnedMesh::on_device_destroyed,
	                                                  this);
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

	EventManager::get_global().register_latch_handler(DeviceCreatedEvent::type_id,
	                                                  &ImportedMesh::on_device_created,
	                                                  &ImportedMesh::on_device_destroyed,
	                                                  this);
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
}

void ImportedMesh::on_device_destroyed(const Event &)
{
	vbo_attributes.reset();
	vbo_position.reset();
	ibo.reset();
}

CubeMesh::CubeMesh()
{
	auto &event = EventManager::get_global();
	event.register_latch_handler(DeviceCreatedEvent::type_id, &CubeMesh::on_device_created, &CubeMesh::on_device_destroyed, this);
	static_aabb = AABB(vec3(-1.0f), vec3(1.0f));
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
	material = MaterialManager::get().request_material("assets://materials/default.json");

	vertex_offset = 0;
	ibo_offset = 0;
	count = 36;
}

void CubeMesh::on_device_destroyed(const Event &)
{
	reset();
}

Skybox::Skybox(std::string bg_path)
	: bg_path(move(bg_path))
{
	EventManager::get_global().register_latch_handler(DeviceCreatedEvent::type_id,
	                                                  &Skybox::on_device_created,
	                                                  &Skybox::on_device_destroyed,
	                                                  this);
}

struct SkyboxRenderInfo : RenderInfo
{
	Program *program;
	const ImageView *view;
	const Sampler *sampler;
};

static void skybox_render(CommandBuffer &cmd, const RenderInfo **infos, unsigned instances)
{
	assert(instances == 1);
	auto *info = static_cast<const SkyboxRenderInfo *>(infos[0]);

	cmd.set_program(*info->program);
	cmd.set_texture(2, 0, *info->view, *info->sampler);

	int8_t *coord = static_cast<int8_t *>(cmd.allocate_vertex_data(0, 8, 2));
	coord[0] = -128;
	coord[1] = -128;
	coord[2] = +127;
	coord[3] = -128;
	coord[4] = -128;
	coord[5] = +127;
	coord[6] = +127;
	coord[7] = +127;
	cmd.set_vertex_attrib(0, 0, VK_FORMAT_R8G8_SNORM, 0);

	cmd.set_cull_mode(VK_CULL_MODE_NONE);
	cmd.set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
	cmd.draw(4);
}

void Skybox::get_render_info(const RenderContext &context, const CachedSpatialTransformComponent *,
                             RenderQueue &queue) const
{
	auto &info = queue.emplace<SkyboxRenderInfo>(Queue::Opaque);
	info.view = &texture->get_image()->get_view();

	Hasher h;
	h.pointer(info.view);
	info.instance_key = h.get();
	info.sorting_key = RenderInfo::get_background_sort_key(Queue::Opaque, 0, 0);
	info.render = skybox_render;
	info.sampler = &context.get_device().get_stock_sampler(StockSampler::LinearClamp);
	info.program = queue.get_shader_suites()[ecast(RenderableType::Skybox)].get_program(DrawPipeline::Opaque, 0, 0).get();
}

void Skybox::on_device_created(const Event &event)
{
	texture = event.as<DeviceCreatedEvent>().get_device().get_texture_manager().request_texture(bg_path);
}

void Skybox::on_device_destroyed(const Event &)
{
	texture = nullptr;
}

struct TexturePlaneInfo : RenderInfo
{
	Vulkan::Program *program;
	const Vulkan::ImageView *view;
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

static void texture_plane_render(CommandBuffer &cmd, const RenderInfo **infos, unsigned instances)
{
	for (unsigned i = 0; i < instances; i++)
	{
		auto &info = *static_cast<const TexturePlaneInfo *>(infos[i]);
		cmd.set_program(*info.program);
		cmd.set_texture(2, 0, *info.view, Vulkan::StockSampler::TrilinearClamp);
		cmd.set_texture(2, 1, *info.normal, Vulkan::StockSampler::TrilinearWrap);
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
	EventManager::get_global().register_latch_handler(DeviceCreatedEvent::type_id,
	                                                  &TexturePlane::on_device_created,
	                                                  &TexturePlane::on_device_destroyed,
	                                                  this);
	EventManager::get_global().register_handler(FrameTickEvent::type_id, &TexturePlane::on_frame_time, this);
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

void TexturePlane::get_render_info(const RenderContext &context, const CachedSpatialTransformComponent *transform,
                                   RenderQueue &queue) const
{
	assert(!transform);
	auto &info = queue.emplace<TexturePlaneInfo>(Queue::Opaque);

	info.view = reflection;
	info.normal = &normalmap->get_image()->get_view();
	info.program = queue.get_shader_suites()[ecast(RenderableType::TexturePlane)].get_program(DrawPipeline::Opaque, 0, 0).get();
	info.push.normal = vec4(normalize(normal), 0.0f);
	info.push.position = vec4(position, 0.0f);
	info.push.dPdx = vec4(dpdx, 0.0f);
	info.push.dPdy = vec4(dpdy, 0.0f);
	info.push.tangent = vec4(normalize(dpdx), 0.0f);
	info.push.bitangent = vec4(normalize(dpdy), 0.0f);
	info.render = texture_plane_render;
	info.push.offset_scale = vec4(vec2(0.03 * elapsed), vec2(2.0f));

	Hasher h;
	h.pointer(info.program);
	info.sorting_key = RenderInfo::get_sort_key(context, Queue::Opaque, h.get(), h.get(), position);
	h.u64(info.view->get_cookie());
	info.instance_key = h.get();
}

}