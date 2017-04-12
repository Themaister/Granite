#include "mesh_util.hpp"
#include "device.hpp"
#include "material_util.hpp"

using namespace Vulkan;

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
		P, P, N, P,
		N, P, P, P,
		P, P, N, P,

	    // Bottom
		N, N, N, P,
		P, N, P, P,
		N, N, N, P,
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
		6, 7, 8, 9, 8, 7,
		12, 13, 14, 15, 14, 13,
		18, 19, 20, 21, 20, 19,
		24, 25, 26, 27, 26, 25,
		30, 31, 32, 33, 32, 31,
	};
	BufferCreateInfo ibo_info = {};
	ibo_info.size = sizeof(indices);
	ibo_info.domain = BufferDomain::Device;
	ibo_info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	ibo = device.create_buffer(ibo_info, indices);
	material = StockMaterials::get().get_checkerboard();
}

void CubeMesh::on_device_destroyed(const Event &)
{
	reset();
}

}