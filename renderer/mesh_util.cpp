#include "mesh_util.hpp"
#include "device.hpp"
#include "material_util.hpp"
#include "material_manager.hpp"
#include "render_context.hpp"
#include "shader_suite.hpp"
#include "renderer.hpp"

using namespace Vulkan;
using namespace Util;

namespace Granite
{
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
	info.sorting_key = RenderInfo::get_background_sort_key(Queue::Opaque, 0);
	info.render = skybox_render;
	info.sampler = &context.get_device().get_stock_sampler(StockSampler::LinearClamp);
	info.program = queue.get_shader_suites()[ecast(RenderableType::Skybox)].get_program(MeshDrawPipeline::Opaque, 0, 0).get();
}

void Skybox::on_device_created(const Event &event)
{
	texture = event.as<DeviceCreatedEvent>().get_device().get_texture_manager().request_texture(bg_path);
}

void Skybox::on_device_destroyed(const Event &)
{
	texture = nullptr;
}

}