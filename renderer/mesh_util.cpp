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

#include "mesh_util.hpp"
#include "device.hpp"
#include "material_util.hpp"
#include "material_manager.hpp"
#include "render_context.hpp"
#include "shader_suite.hpp"
#include "renderer.hpp"
#include "utils/image_utils.hpp"
#include "application_events.hpp"
#include "render_graph.hpp"
#include "simd.hpp"
#include <string.h>

using namespace Vulkan;
using namespace Util;
using namespace Granite::SceneFormats;

namespace Granite
{
ImportedSkinnedMesh::ImportedSkinnedMesh(const Mesh &mesh_, const MaterialInfo &info_)
	: mesh(mesh_), info(info_)
{
	topology = mesh.topology;
	index_type = mesh.index_type;

	position_stride = mesh.position_stride;
	attribute_stride = mesh.attribute_stride;
	memcpy(attributes, mesh.attribute_layout, sizeof(mesh.attribute_layout));

	count = mesh.count;
	vertex_offset = 0;
	ibo_offset = 0;

	material = Util::make_derived_handle<Material, MaterialFile>(info);
	static_aabb = mesh.static_aabb;

	EVENT_MANAGER_REGISTER_LATCH(ImportedSkinnedMesh, on_device_created, on_device_destroyed, DeviceCreatedEvent);
}

void ImportedSkinnedMesh::on_device_created(const DeviceCreatedEvent &created)
{
	auto &device = created.get_device();

	BufferCreateInfo buffer_info = {};
	buffer_info.domain = BufferDomain::Device;
	buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

	buffer_info.size = mesh.positions.size();
	vbo_position = device.create_buffer(buffer_info, mesh.positions.data());

	if (!mesh.attributes.empty())
	{
		buffer_info.size = mesh.attributes.size();
		vbo_attributes = device.create_buffer(buffer_info, mesh.attributes.data());
	}

	if (!mesh.indices.empty())
	{
		buffer_info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
		buffer_info.size = mesh.indices.size();
		ibo = device.create_buffer(buffer_info, mesh.indices.data());
	}

	bake();
}

void ImportedSkinnedMesh::on_device_destroyed(const DeviceCreatedEvent &)
{
	vbo_attributes.reset();
	vbo_position.reset();
	ibo.reset();
}

const SceneFormats::Mesh &ImportedSkinnedMesh::get_mesh() const
{
	return mesh;
}

const SceneFormats::MaterialInfo &ImportedSkinnedMesh::get_material_info() const
{
	return info;
}

ImportedMesh::ImportedMesh(const Mesh &mesh_, const MaterialInfo &info_)
	: mesh(mesh_), info(info_)
{
	topology = mesh.topology;
	primitive_restart = mesh.primitive_restart;
	index_type = mesh.index_type;

	position_stride = mesh.position_stride;
	attribute_stride = mesh.attribute_stride;
	memcpy(attributes, mesh.attribute_layout, sizeof(mesh.attribute_layout));

	count = mesh.count;
	vertex_offset = 0;
	ibo_offset = 0;

	material = Util::make_derived_handle<Material, MaterialFile>(info);
	static_aabb = mesh.static_aabb;

	EVENT_MANAGER_REGISTER_LATCH(ImportedMesh, on_device_created, on_device_destroyed, DeviceCreatedEvent);
}

const SceneFormats::Mesh &ImportedMesh::get_mesh() const
{
	return mesh;
}

const SceneFormats::MaterialInfo &ImportedMesh::get_material_info() const
{
	return info;
}

void ImportedMesh::on_device_created(const DeviceCreatedEvent &created)
{
	auto &device = created.get_device();

	BufferCreateInfo buffer_info = {};
	buffer_info.domain = BufferDomain::Device;
	buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

	buffer_info.size = mesh.positions.size();
	vbo_position = device.create_buffer(buffer_info, mesh.positions.data());

	if (!mesh.attributes.empty())
	{
		buffer_info.size = mesh.attributes.size();
		vbo_attributes = device.create_buffer(buffer_info, mesh.attributes.data());
	}

	if (!mesh.indices.empty())
	{
		buffer_info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
		buffer_info.size = mesh.indices.size();
		ibo = device.create_buffer(buffer_info, mesh.indices.data());
	}

	bake();
}

void ImportedMesh::on_device_destroyed(const DeviceCreatedEvent &)
{
	vbo_attributes.reset();
	vbo_position.reset();
	ibo.reset();
}


GeneratedMeshData create_sphere_mesh(unsigned density)
{
	GeneratedMeshData mesh;
	mesh.positions.reserve(6 * density * density);
	mesh.attributes.reserve(6 * density * density);
	mesh.indices.reserve(2 * density * density * 6);
	mesh.has_uvs = true;

	float density_mod = 1.0f / float(density - 1);
	const auto to_uv = [&](unsigned x, unsigned y) -> vec2 {
		return vec2(density_mod * x, density_mod * y);
	};

	static const vec3 base_pos[6] = {
			vec3(1.0f, 1.0f, 1.0f),
			vec3(-1.0f, 1.0f, -1.0f),
			vec3(-1.0f, 1.0f, -1.0f),
			vec3(-1.0f, -1.0f, +1.0f),
			vec3(-1.0f, 1.0f, +1.0f),
			vec3(+1.0f, 1.0f, -1.0f),
	};

	static const vec3 dx[6] = {
			vec3(0.0f, 0.0f, -2.0f),
			vec3(0.0f, 0.0f, +2.0f),
			vec3(2.0f, 0.0f, 0.0f),
			vec3(2.0f, 0.0f, 0.0f),
			vec3(2.0f, 0.0f, 0.0f),
			vec3(-2.0f, 0.0f, 0.0f),
	};

	static const vec3 dy[6] = {
			vec3(0.0f, -2.0f, 0.0f),
			vec3(0.0f, -2.0f, 0.0f),
			vec3(0.0f, 0.0f, +2.0f),
			vec3(0.0f, 0.0f, -2.0f),
			vec3(0.0f, -2.0f, 0.0f),
			vec3(0.0f, -2.0f, 0.0f),
	};

	// I don't know how many times I've written this exact code in different projects by now. :)
	for (unsigned face = 0; face < 6; face++)
	{
		unsigned index_offset = face * density * density;
		for (unsigned y = 0; y < density; y++)
		{
			for (unsigned x = 0; x < density; x++)
			{
				vec2 uv = to_uv(x, y);
				vec3 pos = normalize(base_pos[face] + dx[face] * uv.x + dy[face] * uv.y);
				mesh.positions.push_back(pos);
				mesh.attributes.push_back({ pos, uv });
			}
		}

		unsigned strips = density - 1;
		for (unsigned y = 0; y < strips; y++)
		{
			unsigned base_index = index_offset + y * density;
			for (unsigned x = 0; x < density; x++)
			{
				mesh.indices.push_back(base_index + x);
				mesh.indices.push_back(base_index + x + density);
			}
			mesh.indices.push_back(0xffff);
		}
	}

	mesh.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
	mesh.primitive_restart = true;
	return mesh;
}

GeneratedMeshData create_capsule_mesh(unsigned density, float height, float radius)
{
	GeneratedMeshData mesh;
	mesh.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	mesh.primitive_restart = false;

	const unsigned inner_rings = density / 2;
	mesh.positions.resize(2 * inner_rings * density + 2);
	mesh.attributes.resize(2 * inner_rings * density + 2);

	const float half_height = 0.5f * height - 0.5f * radius;

	// Top center
	mesh.positions[0] = vec3(0.0f, half_height + radius, 0.0f);
	mesh.attributes[0].normal = vec3(0.0f, 1.0f, 0.0f);
	// Bottom center
	mesh.positions[1] = vec3(0.0f, -half_height - radius, 0.0f);
	mesh.attributes[1].normal = vec3(0.0f, -1.0f, 0.0f);

	float inv_density = 1.0f / float(density);

	// Top rings
	for (unsigned ring = 0; ring < inner_rings; ring++)
	{
		float w = float(ring + 1) / float(inner_rings);
		float extra_h = radius * muglm::sqrt(1.0f - w * w);
		unsigned offset = ring * density + 2;
		for (unsigned i = 0; i < density; i++)
		{
			float rad = 2.0f * pi<float>() * (i + 0.5f) * inv_density;
			auto &pos = mesh.positions[offset + i];
			pos = vec3(w * radius * cos(rad), half_height + extra_h, -w * radius * sin(rad));
			mesh.attributes[offset + i].normal = normalize(vec3(pos.x, extra_h, pos.z));
		}
	}

	// Bottom rings
	for (unsigned ring = 0; ring < inner_rings; ring++)
	{
		float w = float(inner_rings - ring) / float(inner_rings);
		float extra_h = radius * muglm::sqrt(1.0f - w * w);
		unsigned offset = (ring + inner_rings) * density + 2;
		for (unsigned i = 0; i < density; i++)
		{
			float rad = 2.0f * pi<float>() * (i + 0.5f) * inv_density;
			auto &pos = mesh.positions[offset + i];
			pos = vec3(w * radius * cos(rad), -half_height - extra_h, -w * radius * sin(rad));
			mesh.attributes[offset + i].normal = normalize(vec3(pos.x, -extra_h, pos.z));
		}
	}

	// Link up top vertices.
	for (unsigned i = 0; i < density; i++)
	{
		mesh.indices.push_back(0);
		mesh.indices.push_back(i + 2);
		mesh.indices.push_back(((i + 1) % density) + 2);
	}

	// Link up bottom vertices.
	for (unsigned i = 0; i < density; i++)
	{
		mesh.indices.push_back(1);
		mesh.indices.push_back((2 * inner_rings - 1) * density + ((i + 1) % density) + 2);
		mesh.indices.push_back((2 * inner_rings - 1) * density + i + 2);
	}

	// Link up rings.
	for (unsigned ring = 0; ring < 2 * inner_rings - 1; ring++)
	{
		unsigned off0 = ring * density + 2;
		unsigned off1 = off0 + density;
		for (unsigned i = 0; i < density; i++)
		{
			mesh.indices.push_back(off0 + i);
			mesh.indices.push_back(off1 + i);
			mesh.indices.push_back(off0 + ((i + 1) % density));
			mesh.indices.push_back(off1 + ((i + 1) % density));
			mesh.indices.push_back(off0 + ((i + 1) % density));
			mesh.indices.push_back(off1 + i);
		}
	}

	return mesh;
}

GeneratedMeshData create_cylinder_mesh(unsigned density, float height, float radius)
{
	GeneratedMeshData mesh;
	mesh.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	mesh.primitive_restart = false;

	mesh.positions.resize(6 * density + 2);
	mesh.attributes.resize(6 * density + 2);

	const float half_height = 0.5f * height;

	// Top center
	mesh.positions[0] = vec3(0.0f, half_height, 0.0f);
	mesh.attributes[0].normal = vec3(0.0f, 1.0f, 0.0f);
	// Bottom center
	mesh.positions[1] = vec3(0.0f, -half_height, 0.0f);
	mesh.attributes[1].normal = vec3(0.0f, -1.0f, 0.0f);

	float inv_density = 1.0f / float(density);

	const float high_ring_h = 0.95f * half_height;
	const float low_ring_h = -0.95f * half_height;

	// Top ring inner
	for (unsigned i = 0; i < density; i++)
	{
		float rad = 2.0f * pi<float>() * (i + 0.5f) * inv_density;
		mesh.positions[i + 2] = vec3(0.95f * radius * cos(rad), half_height, -0.95f * radius * sin(rad));
		mesh.attributes[i + 2].normal = vec3(0.0f, 1.0f, 0.0f);
	}

	// Top ring
	for (unsigned i = 0; i < density; i++)
	{
		float rad = 2.0f * pi<float>() * (i + 0.5f) * inv_density;
		mesh.positions[density + i + 2] = vec3(radius * cos(rad), half_height, -radius * sin(rad));
		mesh.attributes[density + i + 2].normal = normalize(vec3(cos(rad), 1.0f, -sin(rad)));
	}

	// High ring
	for (unsigned i = 0; i < density; i++)
	{
		float rad = 2.0f * pi<float>() * (i + 0.5f) * inv_density;
		mesh.positions[2 * density + i + 2] = vec3(radius * cos(rad), high_ring_h, -radius * sin(rad));
		mesh.attributes[2 * density + i + 2].normal = vec3(cos(rad), 0.0f, -sin(rad));
	}

	// Low ring
	for (unsigned i = 0; i < density; i++)
	{
		float rad = 2.0f * pi<float>() * (i + 0.5f) * inv_density;
		mesh.positions[3 * density + i + 2] = vec3(radius * cos(rad), low_ring_h, -radius * sin(rad));
		mesh.attributes[3 * density + i + 2].normal = vec3(cos(rad), 0.0f, -sin(rad));
	}

	// Bottom ring
	for (unsigned i = 0; i < density; i++)
	{
		float rad = 2.0f * pi<float>() * (i + 0.5f) * inv_density;
		mesh.positions[4 * density + i + 2] = vec3(radius * cos(rad), -half_height, -radius * sin(rad));
		mesh.attributes[4 * density + i + 2].normal = normalize(vec3(cos(rad), -1.0f, -sin(rad)));
	}

	// Bottom inner ring
	for (unsigned i = 0; i < density; i++)
	{
		float rad = 2.0f * pi<float>() * (i + 0.5f) * inv_density;
		mesh.positions[5 * density + i + 2] = vec3(0.95f * radius * cos(rad), -half_height, 0.95f * -radius * sin(rad));
		mesh.attributes[5 * density + i + 2].normal = vec3(0.0f, -1.0f, 0.0f);
	}

	// Link up top vertices.
	for (unsigned i = 0; i < density; i++)
	{
		mesh.indices.push_back(0);
		mesh.indices.push_back(i + 2);
		mesh.indices.push_back(((i + 1) % density) + 2);
	}

	// Link up bottom vertices.
	for (unsigned i = 0; i < density; i++)
	{
		mesh.indices.push_back(1);
		mesh.indices.push_back(5 * density + ((i + 1) % density) + 2);
		mesh.indices.push_back(5 * density + i + 2);
	}

	// Link up rings.
	for (unsigned ring = 0; ring < 5; ring++)
	{
		unsigned off0 = ring * density + 2;
		unsigned off1 = off0 + density;
		for (unsigned i = 0; i < density; i++)
		{
			mesh.indices.push_back(off0 + i);
			mesh.indices.push_back(off1 + i);
			mesh.indices.push_back(off0 + ((i + 1) % density));
			mesh.indices.push_back(off1 + ((i + 1) % density));
			mesh.indices.push_back(off0 + ((i + 1) % density));
			mesh.indices.push_back(off1 + i);
		}
	}

	return mesh;
}

GeneratedMeshData create_cone_mesh(unsigned density, float height, float radius)
{
	GeneratedMeshData mesh;
	mesh.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	mesh.primitive_restart = false;

	mesh.positions.resize(4 * density + 2);
	mesh.attributes.resize(4 * density + 2);

	// Top center
	mesh.positions[0] = vec3(0.0f, height, 0.0f);
	mesh.attributes[0].normal = vec3(0.0f, 1.0f, 0.0f);
	// Bottom center
	mesh.positions[1] = vec3(0.0f, 0.0f, 0.0f);
	mesh.attributes[1].normal = vec3(0.0f, -1.0f, 0.0f);

	float inv_density = 1.0f / float(density);

	const float top_ring_h = 0.95f * height;
	const float top_ring_r = (1.0f - 0.95f) * radius;
	const float low_ring_h = 0.05f * height;
	const float low_ring_r = (1.0f - 0.05f) * radius;

	// Top ring
	for (unsigned i = 0; i < density; i++)
	{
		float rad = 2.0f * pi<float>() * (i + 0.5f) * inv_density;
		mesh.positions[i + 2] = vec3(top_ring_r * cos(rad), top_ring_h, -top_ring_r * sin(rad));
		mesh.attributes[i + 2].normal = normalize(vec3(height * cos(rad), radius, -height * sin(rad)));
	}

	// Low ring
	for (unsigned i = 0; i < density; i++)
	{
		float rad = 2.0f * pi<float>() * (i + 0.5f) * inv_density;
		mesh.positions[density + i + 2] = vec3(low_ring_r * cos(rad), low_ring_h, -low_ring_r * sin(rad));
		mesh.attributes[density + i + 2].normal = normalize(vec3(height * cos(rad), radius, -height * sin(rad)));
	}

	// Bottom ring
	for (unsigned i = 0; i < density; i++)
	{
		float rad = 2.0f * pi<float>() * (i + 0.5f) * inv_density;
		mesh.positions[2 * density + i + 2] = vec3(radius * cos(rad), 0.0f, -radius * sin(rad));
		mesh.attributes[2 * density + i + 2].normal = normalize(vec3(cos(rad), 0.0f, -sin(rad)));
	}

	// Inner ring
	for (unsigned i = 0; i < density; i++)
	{
		float rad = 2.0f * pi<float>() * (i + 0.5f) * inv_density;
		mesh.positions[3 * density + i + 2] = vec3(0.95f * radius * cos(rad), 0.0f, 0.95f * -radius * sin(rad));
		mesh.attributes[3 * density + i + 2].normal = vec3(0.0f, -1.0f, 0.0f);
	}

	for (auto &pos : mesh.positions)
		pos -= vec3(0.0f, 0.5f * height, 0.0f);

	// Link up top vertices.
	for (unsigned i = 0; i < density; i++)
	{
		mesh.indices.push_back(0);
		mesh.indices.push_back(i + 2);
		mesh.indices.push_back(((i + 1) % density) + 2);
	}

	// Link up bottom vertices.
	for (unsigned i = 0; i < density; i++)
	{
		mesh.indices.push_back(1);
		mesh.indices.push_back(3 * density + ((i + 1) % density) + 2);
		mesh.indices.push_back(3 * density + i + 2);
	}

	// Link up top and low rings.
	for (unsigned i = 0; i < density; i++)
	{
		mesh.indices.push_back(i + 2);
		mesh.indices.push_back(density + i + 2);
		mesh.indices.push_back(((i + 1) % density) + 2);
		mesh.indices.push_back(density + ((i + 1) % density) + 2);
		mesh.indices.push_back(((i + 1) % density) + 2);
		mesh.indices.push_back(density + i + 2);
	}

	// Link up low and bottom rings.
	for (unsigned i = 0; i < density; i++)
	{
		mesh.indices.push_back(density + i + 2);
		mesh.indices.push_back(2 * density + i + 2);
		mesh.indices.push_back(density + ((i + 1) % density) + 2);
		mesh.indices.push_back(2 * density + ((i + 1) % density) + 2);
		mesh.indices.push_back(density + ((i + 1) % density) + 2);
		mesh.indices.push_back(2 * density + i + 2);
	}

	// Link up bottom and inner rings.
	for (unsigned i = 0; i < density; i++)
	{
		mesh.indices.push_back(2 * density + i + 2);
		mesh.indices.push_back(3 * density + i + 2);
		mesh.indices.push_back(2 * density + ((i + 1) % density) + 2);
		mesh.indices.push_back(3 * density + ((i + 1) % density) + 2);
		mesh.indices.push_back(2 * density + ((i + 1) % density) + 2);
		mesh.indices.push_back(3 * density + i + 2);
	}

	return mesh;
}

void GeneratedMesh::setup_from_generated_mesh(Vulkan::Device &device, const GeneratedMeshData &mesh)
{
	BufferCreateInfo info = {};
	info.size = mesh.positions.size() * sizeof(vec3);
	info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	info.domain = BufferDomain::Device;
	vbo_position = device.create_buffer(info, mesh.positions.data());

	info.size = mesh.attributes.size() * sizeof(GeneratedMeshData::Attribute);
	vbo_attributes = device.create_buffer(info, mesh.attributes.data());

	this->attributes[ecast(MeshAttribute::Position)].format = VK_FORMAT_R32G32B32_SFLOAT;
	this->attributes[ecast(MeshAttribute::Position)].offset = 0;
	this->attributes[ecast(MeshAttribute::Normal)].format = VK_FORMAT_R32G32B32_SFLOAT;
	this->attributes[ecast(MeshAttribute::Normal)].offset = offsetof(GeneratedMeshData::Attribute, normal);
	if (mesh.has_uvs)
	{
		this->attributes[ecast(MeshAttribute::UV)].format = VK_FORMAT_R32G32_SFLOAT;
		this->attributes[ecast(MeshAttribute::UV)].offset = offsetof(GeneratedMeshData::Attribute, uv);
	}
	position_stride = sizeof(vec3);
	attribute_stride = sizeof(GeneratedMeshData::Attribute);

	info.size = mesh.indices.size() * sizeof(uint16_t);
	info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	ibo = device.create_buffer(info, mesh.indices.data());
	ibo_offset = 0;
	index_type = VK_INDEX_TYPE_UINT16;
	count = mesh.indices.size();
	topology = mesh.topology;
	primitive_restart = mesh.primitive_restart;

	bake();
}

SphereMesh::SphereMesh(unsigned density_)
	: density(density_)
{
	static_aabb = AABB(vec3(-1.0f), vec3(1.0f));
	material = StockMaterials::get().get_checkerboard();
	EVENT_MANAGER_REGISTER_LATCH(SphereMesh, on_device_created, on_device_destroyed, DeviceCreatedEvent);
}

void SphereMesh::on_device_created(const DeviceCreatedEvent &event)
{
	auto &device = event.get_device();
	auto mesh = create_sphere_mesh(density);
	setup_from_generated_mesh(device, mesh);
}

void SphereMesh::on_device_destroyed(const DeviceCreatedEvent &)
{
	reset();
}

ConeMesh::ConeMesh(unsigned density_, float height_, float radius_)
	: density(density_), height(height_), radius(radius_)
{
	static_aabb = AABB(vec3(-1.0f), vec3(1.0f));
	material = StockMaterials::get().get_checkerboard();
	EVENT_MANAGER_REGISTER_LATCH(ConeMesh, on_device_created, on_device_destroyed, DeviceCreatedEvent);
}

void ConeMesh::on_device_created(const DeviceCreatedEvent &event)
{
	auto &device = event.get_device();
	auto mesh = create_cone_mesh(density, height, radius);
	setup_from_generated_mesh(device, mesh);
}

void ConeMesh::on_device_destroyed(const DeviceCreatedEvent &)
{
	reset();
}

CylinderMesh::CylinderMesh(unsigned density_, float height_, float radius_)
	: density(density_), height(height_), radius(radius_)
{
	static_aabb = AABB(vec3(-1.0f), vec3(1.0f));
	material = StockMaterials::get().get_checkerboard();
	EVENT_MANAGER_REGISTER_LATCH(CylinderMesh, on_device_created, on_device_destroyed, DeviceCreatedEvent);
}

void CylinderMesh::on_device_created(const DeviceCreatedEvent &event)
{
	auto &device = event.get_device();
	auto mesh = create_cylinder_mesh(density, height, radius);
	setup_from_generated_mesh(device, mesh);
}

void CylinderMesh::on_device_destroyed(const DeviceCreatedEvent &)
{
	reset();
}

CapsuleMesh::CapsuleMesh(unsigned density_, float height_, float radius_)
	: density(density_), height(height_), radius(radius_)
{
	static_aabb = AABB(vec3(-1.0f), vec3(1.0f));
	material = StockMaterials::get().get_checkerboard();
	EVENT_MANAGER_REGISTER_LATCH(CapsuleMesh, on_device_created, on_device_destroyed, DeviceCreatedEvent);
}

void CapsuleMesh::on_device_created(const DeviceCreatedEvent &event)
{
	auto &device = event.get_device();
	auto mesh = create_capsule_mesh(density, height, radius);
	setup_from_generated_mesh(device, mesh);
}

void CapsuleMesh::on_device_destroyed(const DeviceCreatedEvent &)
{
	reset();
}

CubeMesh::CubeMesh()
{
	static_aabb = AABB(vec3(-1.0f), vec3(1.0f));
	material = StockMaterials::get().get_checkerboard();
	EVENT_MANAGER_REGISTER_LATCH(CubeMesh, on_device_created, on_device_destroyed, DeviceCreatedEvent);
}

namespace CubeData
{
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
	0, 0, P, 0, P, 0, 0, 0, 0, P, 0, 0,
	0, 0, P, 0, P, 0, 0, 0, P, P, 0, 0,
	0, 0, P, 0, P, 0, 0, 0, 0, 0, 0, 0,
	0, 0, P, 0, P, 0, 0, 0, P, 0, 0, 0,

	// Far
	0, 0, N, 0, N, 0, 0, 0, 0, P, 0, 0,
	0, 0, N, 0, N, 0, 0, 0, P, P, 0, 0,
	0, 0, N, 0, N, 0, 0, 0, 0, 0, 0, 0,
	0, 0, N, 0, N, 0, 0, 0, P, 0, 0, 0,

	// Left
	N, 0, 0, 0, 0, 0, P, 0, 0, P, 0, 0,
	N, 0, 0, 0, 0, 0, P, 0, P, P, 0, 0,
	N, 0, 0, 0, 0, 0, P, 0, 0, 0, 0, 0,
	N, 0, 0, 0, 0, 0, P, 0, P, 0, 0, 0,

	// Right
	P, 0, 0, 0, 0, 0, N, 0, 0, P, 0, 0,
	P, 0, 0, 0, 0, 0, N, 0, P, P, 0, 0,
	P, 0, 0, 0, 0, 0, N, 0, 0, 0, 0, 0,
	P, 0, 0, 0, 0, 0, N, 0, P, 0, 0, 0,

	// Top
	0, P, 0, 0, P, 0, 0, 0, 0, P, 0, 0,
	0, P, 0, 0, P, 0, 0, 0, P, P, 0, 0,
	0, P, 0, 0, P, 0, 0, 0, 0, 0, 0, 0,
	0, P, 0, 0, P, 0, 0, 0, P, 0, 0, 0,

	// Bottom
	0, N, 0, 0, P, 0, 0, 0, 0, P, 0, 0,
	0, N, 0, 0, P, 0, 0, 0, P, P, 0, 0,
	0, N, 0, 0, P, 0, 0, 0, 0, 0, 0, 0,
	0, N, 0, 0, P, 0, 0, 0, P, 0, 0, 0,
};

static const uint16_t indices[] = {
	0, 1, 2, 3, 2, 1,
	4, 5, 6, 7, 6, 5,
	8, 9, 10, 11, 10, 9,
	12, 13, 14, 15, 14, 13,
	16, 17, 18, 19, 18, 17,
	20, 21, 22, 23, 22, 21,
};
}

Mesh CubeMesh::build_plain_mesh()
{
	Mesh mesh;

	mesh.position_stride = 4;
	mesh.positions.resize(sizeof(CubeData::positions));
	memcpy(mesh.positions.data(), CubeData::positions, sizeof(CubeData::positions));

	mesh.attribute_layout[ecast(MeshAttribute::Position)].offset = 0;
	mesh.attribute_layout[ecast(MeshAttribute::Position)].format = VK_FORMAT_R8G8B8A8_SNORM;
	mesh.attribute_layout[ecast(MeshAttribute::Normal)].offset = 0;
	mesh.attribute_layout[ecast(MeshAttribute::Normal)].format = VK_FORMAT_R8G8B8A8_SNORM;
	mesh.attribute_layout[ecast(MeshAttribute::Tangent)].offset = 4;
	mesh.attribute_layout[ecast(MeshAttribute::Tangent)].format = VK_FORMAT_R8G8B8A8_SNORM;
	mesh.attribute_layout[ecast(MeshAttribute::UV)].offset = 8;
	mesh.attribute_layout[ecast(MeshAttribute::UV)].format = VK_FORMAT_R8G8B8A8_SNORM;
	mesh.attribute_stride = 12;

	mesh.attributes.resize(sizeof(CubeData::attr));
	memcpy(mesh.attributes.data(), CubeData::attr, sizeof(CubeData::attr));

	mesh.index_type = VK_INDEX_TYPE_UINT16;
	mesh.indices.resize(sizeof(CubeData::indices));
	mesh.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	mesh.count = sizeof(CubeData::indices) / sizeof(CubeData::indices[0]);
	memcpy(mesh.indices.data(), CubeData::indices, sizeof(CubeData::indices));
	return mesh;
}

void CubeMesh::on_device_created(const DeviceCreatedEvent &created)
{
	auto &device = created.get_device();

	BufferCreateInfo vbo_info = {};
	vbo_info.domain = BufferDomain::Device;
	vbo_info.size = sizeof(CubeData::positions);
	vbo_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	vbo_position = device.create_buffer(vbo_info, CubeData::positions);
	position_stride = 4;

	attributes[ecast(MeshAttribute::Position)].offset = 0;
	attributes[ecast(MeshAttribute::Position)].format = VK_FORMAT_R8G8B8A8_SNORM;

	attributes[ecast(MeshAttribute::Normal)].offset = 0;
	attributes[ecast(MeshAttribute::Normal)].format = VK_FORMAT_R8G8B8A8_SNORM;
	attributes[ecast(MeshAttribute::Tangent)].offset = 4;
	attributes[ecast(MeshAttribute::Tangent)].format = VK_FORMAT_R8G8B8A8_SNORM;
	attributes[ecast(MeshAttribute::UV)].offset = 8;
	attributes[ecast(MeshAttribute::UV)].format = VK_FORMAT_R8G8B8A8_SNORM;
	attribute_stride = 12;

	vbo_info.size = sizeof(CubeData::attr);
	vbo_attributes = device.create_buffer(vbo_info, CubeData::attr);

	BufferCreateInfo ibo_info = {};
	ibo_info.size = sizeof(CubeData::indices);
	ibo_info.domain = BufferDomain::Device;
	ibo_info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	ibo = device.create_buffer(ibo_info, CubeData::indices);

	vertex_offset = 0;
	ibo_offset = 0;
	count = 36;
	bake();
}

void CubeMesh::on_device_destroyed(const DeviceCreatedEvent &)
{
	reset();
}

SkyCylinder::SkyCylinder(std::string bg_path_)
	: bg_path(move(bg_path_))
{
	EVENT_MANAGER_REGISTER_LATCH(SkyCylinder, on_device_created, on_device_destroyed, DeviceCreatedEvent);
}

struct SkyCylinderRenderInfo
{
	Program *program;
	const ImageView *view;
	const Sampler *sampler;
	vec3 color;
	float scale;

	const Buffer *vbo;
	const Buffer *ibo;
	unsigned count;
};

struct CylinderVertex
{
	vec3 pos;
	vec2 uv;
};

static void skycylinder_render(CommandBuffer &cmd, const RenderQueueData *infos, unsigned instances)
{
	cmd.set_stencil_test(true);
	cmd.set_stencil_reference(0xff, 0xff, 1);
	cmd.set_stencil_ops(VK_COMPARE_OP_ALWAYS, VK_STENCIL_OP_REPLACE, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP);

	for (unsigned i = 0; i < instances; i++)
	{
		auto *info = static_cast<const SkyCylinderRenderInfo *>(infos[i].render_info);

		cmd.set_program(info->program);
		cmd.set_texture(2, 0, *info->view, *info->sampler);

		vec4 color_scale(info->color, info->scale);
		cmd.push_constants(&color_scale, 0, sizeof(color_scale));

		auto vp = cmd.get_viewport();
		vp.minDepth = 1.0f;
		vp.maxDepth = 1.0f;
		cmd.set_viewport(vp);

		cmd.set_vertex_attrib(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(CylinderVertex, pos));
		cmd.set_vertex_attrib(1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(CylinderVertex, uv));
		cmd.set_vertex_binding(0, *info->vbo, 0, sizeof(CylinderVertex));
		cmd.set_index_buffer(*info->ibo, 0, VK_INDEX_TYPE_UINT16);
		cmd.set_primitive_restart(true);
		cmd.set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
		cmd.draw_indexed(info->count);
	}
}

void SkyCylinder::on_device_created(const DeviceCreatedEvent &created)
{
	auto &device = created.get_device();
	texture = nullptr;
	if (!bg_path.empty())
		texture = device.get_texture_manager().request_texture(bg_path);

	std::vector<CylinderVertex> v;
	std::vector<uint16_t> indices;
	for (unsigned i = 0; i < 33; i++)
	{
		float x = cos(2.0f * pi<float>() * i / 32.0f);
		float z = sin(2.0f * pi<float>() * i / 32.0f);
		v.push_back({ vec3(x, +1.0f, z), vec2(i / 32.0f, 0.0f) });
		v.push_back({ vec3(x, -1.0f, z), vec2(i / 32.0f, 1.0f) });
	}

	for (unsigned i = 0; i < 33; i++)
	{
		indices.push_back(2 * i + 0);
		indices.push_back(2 * i + 1);
	}

	indices.push_back(0xffff);

	unsigned ring_offset = v.size();
	v.push_back({ vec3(0.0f, 1.0f, 0.0f), vec2(0.5f, 0.0f) });
	v.push_back({ vec3(0.0f, -1.0f, 0.0f), vec2(0.5f, 1.0f) });

	for (unsigned i = 0; i < 32; i++)
	{
		indices.push_back(ring_offset);
		indices.push_back(2 * i);
		indices.push_back(2 * (i + 1));
		indices.push_back(0xffff);
	}

	for (unsigned i = 0; i < 32; i++)
	{
		indices.push_back(ring_offset + 1);
		indices.push_back(2 * (i + 1) + 1);
		indices.push_back(2 * i + 1);
		indices.push_back(0xffff);
	}

	BufferCreateInfo info = {};
	info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	info.size = v.size() * sizeof(CylinderVertex);
	info.domain = BufferDomain::Device;
	vbo = device.create_buffer(info, v.data());

	info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	info.size = indices.size() * sizeof(uint16_t);
	ibo = device.create_buffer(info, indices.data());

	count = indices.size();

	Vulkan::SamplerCreateInfo sampler_info = {};
	sampler_info.address_mode_u = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	sampler_info.address_mode_v = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_info.address_mode_w = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_info.max_anisotropy = 1.0f;
	sampler_info.mag_filter = VK_FILTER_LINEAR;
	sampler_info.min_filter = VK_FILTER_LINEAR;
	sampler_info.mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	sampler_info.max_lod = VK_LOD_CLAMP_NONE;
	sampler = device.create_sampler(sampler_info);
}

void SkyCylinder::on_device_destroyed(const DeviceCreatedEvent &)
{
	texture = nullptr;
	vbo.reset();
	ibo.reset();
	sampler.reset();
}

void SkyCylinder::get_render_info(const RenderContext &, const RenderInfoComponent *,
                                  RenderQueue &queue) const
{
	SkyCylinderRenderInfo info;

	info.view = &texture->get_image()->get_view();

	Hasher h;
	h.pointer(info.view);

	auto instance_key = h.get();
	auto sorting_key = RenderInfo::get_background_sort_key(Queue::OpaqueEmissive, 0, 0);
	info.sampler = sampler.get();
	info.color = color;
	info.scale = scale;

	info.ibo = ibo.get();
	info.vbo = vbo.get();
	info.count = count;

	auto *cylinder_info = queue.push<SkyCylinderRenderInfo>(Queue::OpaqueEmissive, instance_key, sorting_key,
	                                                        skycylinder_render,
	                                                        nullptr);

	if (cylinder_info)
	{
		info.program = queue.get_shader_suites()[ecast(RenderableType::SkyCylinder)].get_program(DrawPipeline::Opaque, 0, 0);
		*cylinder_info = info;
	}
}

Skybox::Skybox(std::string bg_path_, bool latlon)
	: bg_path(move(bg_path_)), is_latlon(latlon)
{
	EVENT_MANAGER_REGISTER_LATCH(Skybox, on_device_created, on_device_destroyed, DeviceCreatedEvent);
}

struct SkyboxRenderInfo
{
	Program *program;
	const ImageView *view;
	const Sampler *sampler;
	vec3 color;
};

static void skybox_render(CommandBuffer &cmd, const RenderQueueData *infos, unsigned instances)
{
	cmd.set_stencil_test(true);
	cmd.set_stencil_reference(0xff, 0xff, 1);
	cmd.set_stencil_ops(VK_COMPARE_OP_ALWAYS, VK_STENCIL_OP_REPLACE, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP);

	for (unsigned i = 0; i < instances; i++)
	{
		auto *info = static_cast<const SkyboxRenderInfo *>(infos[i].render_info);

		cmd.set_program(info->program);

		if (info->view)
			cmd.set_texture(2, 0, *info->view, *info->sampler);

		cmd.push_constants(&info->color, 0, sizeof(info->color));

		CommandBufferUtil::set_fullscreen_quad_vertex_state(cmd);
		CommandBufferUtil::draw_fullscreen_quad(cmd);
	}
}

void Skybox::get_render_info(const RenderContext &context, const RenderInfoComponent *,
                             RenderQueue &queue) const
{
	SkyboxRenderInfo info;

	if (image)
		info.view = &image->get_view();
	else if (texture)
		info.view = &texture->get_image()->get_view();
	else
		info.view = nullptr;

	Hasher h;
	if (info.view)
		h.u64(info.view->get_cookie());
	else
		h.u32(0);

	auto instance_key = h.get();
	auto sorting_key = RenderInfo::get_background_sort_key(Queue::OpaqueEmissive, 0, 0);

	info.sampler = &context.get_device().get_stock_sampler(StockSampler::LinearClamp);
	info.color = color;

	auto *skydome_info = queue.push<SkyboxRenderInfo>(Queue::OpaqueEmissive, instance_key, sorting_key,
	                                                  skybox_render,
	                                                  nullptr);

	if (skydome_info)
	{
		auto shader_flags = info.view ? MATERIAL_EMISSIVE_BIT : 0;
		info.program = queue.get_shader_suites()[ecast(RenderableType::Skybox)].get_program(DrawPipeline::Opaque, 0, shader_flags);
		*skydome_info = info;
	}
}

void Skybox::on_device_created(const Vulkan::DeviceCreatedEvent &created)
{
	texture = nullptr;
	device = &created.get_device();

	if (!bg_path.empty())
	{
		if (is_latlon)
		{
			auto &texture_manager = created.get_device().get_texture_manager();
			texture_manager.request_texture(bg_path);

			auto cube_path = bg_path + ".cube";
			texture = texture_manager.register_deferred_texture(cube_path);

			texture_manager.register_texture_update_notification(bg_path, [this](Vulkan::Texture &tex) {
				texture->replace_image(convert_equirect_to_cube(*device, tex.get_image()->get_view(), 1.0f));
			});
		}
		else
			texture = created.get_device().get_texture_manager().request_texture(bg_path);
	}
}

void Skybox::set_image(Vulkan::ImageHandle skybox)
{
	image = std::move(skybox);
}

void Skybox::set_image(Vulkan::Texture *skybox)
{
	texture = skybox;
}

void Skybox::on_device_destroyed(const DeviceCreatedEvent &)
{
	device = nullptr;
	texture = nullptr;
	image.reset();
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
		vec4 base_emissive;
	};
	Push push;
};

static void texture_plane_render(CommandBuffer &cmd, const RenderQueueData *infos, unsigned instances)
{
	for (unsigned i = 0; i < instances; i++)
	{
		auto &info = *static_cast<const TexturePlaneInfo *>(infos[i].render_info);
		cmd.set_program(info.program);
		if (info.reflection)
			cmd.set_texture(2, 0, *info.reflection, Vulkan::StockSampler::TrilinearClamp);
		if (info.refraction)
			cmd.set_texture(2, 1, *info.refraction, Vulkan::StockSampler::TrilinearClamp);
		cmd.set_texture(2, 2, *info.normal, Vulkan::StockSampler::TrilinearWrap);
		CommandBufferUtil::set_quad_vertex_state(cmd);
		cmd.set_cull_mode(VK_CULL_MODE_NONE);
		cmd.push_constants(&info.push, 0, sizeof(info.push));
		CommandBufferUtil::draw_quad(cmd);
	}
}

TexturePlane::TexturePlane(const std::string &normal_)
	: normal_path(normal_)
{
	EVENT_MANAGER_REGISTER_LATCH(TexturePlane, on_device_created, on_device_destroyed, DeviceCreatedEvent);
	EVENT_MANAGER_REGISTER(TexturePlane, on_frame_time, FrameTickEvent);
}

bool TexturePlane::on_frame_time(const FrameTickEvent &tick)
{
	elapsed = tick.get_elapsed_time();
	return true;
}

void TexturePlane::on_device_created(const DeviceCreatedEvent &created)
{
	normalmap = created.get_device().get_texture_manager().request_texture(normal_path);
}

void TexturePlane::on_device_destroyed(const DeviceCreatedEvent &)
{
	normalmap = nullptr;
}

void TexturePlane::setup_render_pass_resources(RenderGraph &graph)
{
	reflection = nullptr;
	refraction = nullptr;

	if (need_reflection)
		reflection = &graph.get_physical_texture_resource(graph.get_texture_resource(reflection_name).get_physical_index());
	if (need_refraction)
		refraction = &graph.get_physical_texture_resource(graph.get_texture_resource(refraction_name).get_physical_index());
}

void TexturePlane::setup_render_pass_dependencies(RenderGraph &, RenderPass &target)
{
	if (need_reflection)
		target.add_texture_input(reflection_name);
	if (need_refraction)
		target.add_texture_input(refraction_name);
}

void TexturePlane::set_scene(Scene *scene_)
{
	scene = scene_;
}

void TexturePlane::render_main_pass(Vulkan::CommandBuffer &cmd, const mat4 &proj, const mat4 &view)
{
	LightingParameters lighting = *base_context->get_lighting_parameters();
	lighting.shadows = nullptr;
	lighting.cluster = nullptr;

	context.set_lighting_parameters(&lighting);
	context.set_camera(proj, view);

	visible.clear();
	scene->gather_visible_opaque_renderables(context.get_visibility_frustum(), visible);
	scene->gather_visible_transparent_renderables(context.get_visibility_frustum(), visible);
	scene->gather_unbounded_renderables(visible);

	// FIXME: Need to rethink this. We shouldn't be allowed to mutate the renderer suite.
	LOGE("FIXME, TexturePlane::render_main_pass\n");
	auto &renderer = renderer_suite->get_renderer(RendererSuite::Type::ForwardOpaque);
	//renderer.set_mesh_renderer_options_from_lighting(lighting);
	renderer.begin(internal_queue);
	internal_queue.push_renderables(context, visible);
	renderer.flush(cmd, internal_queue, context);
}

void TexturePlane::set_plane(const vec3 &position_, const vec3 &normal_, const vec3 &up_, float extent_up,
                             float extent_across)
{
	position = position_;
	normal = normal_;
	up = up_;
	rad_up = extent_up;
	rad_x = extent_across;

	dpdx = normalize(cross(normal, up)) * extent_across;
	dpdy = normalize(up) * -extent_up;
}

void TexturePlane::set_zfar(float zfar_)
{
	zfar = zfar_;
}

void TexturePlane::add_render_pass(RenderGraph &graph, Type type)
{
	auto &device = graph.get_device();
	bool supports_32bpp =
			device.image_format_is_supported(VK_FORMAT_B10G11R11_UFLOAT_PACK32,
			                                 VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT);

	AttachmentInfo color, depth, reflection_blur;
	color.format = supports_32bpp ? VK_FORMAT_B10G11R11_UFLOAT_PACK32 : VK_FORMAT_R16G16B16A16_SFLOAT;
	depth.format = device.get_default_depth_format();

	color.size_x = scale_x;
	color.size_y = scale_y;
	depth.size_x = scale_x;
	depth.size_y = scale_y;

	reflection_blur.size_x = 0.5f * scale_x;
	reflection_blur.size_y = 0.5f * scale_y;
	reflection_blur.levels = 0;

	auto &name = type == Reflection ? reflection_name : refraction_name;

	auto &lighting = graph.add_pass(name + "-lighting", RENDER_GRAPH_QUEUE_GRAPHICS_BIT);
	lighting.add_color_output(name + "-HDR", color);
	lighting.set_depth_stencil_output(name + "-depth", depth);

	lighting.set_get_clear_depth_stencil([](VkClearDepthStencilValue *value) -> bool {
		if (value)
		{
			value->depth = 1.0f;
			value->stencil = 0;
		}
		return true;
	});

	lighting.set_get_clear_color([](unsigned, VkClearColorValue *value) -> bool {
		if (value)
			memset(value, 0, sizeof(*value));
		return true;
	});

#if 0
	lighting.set_need_render_pass([this]() -> bool {
		// No point in rendering reflection/refraction if we cannot even see it :)
		vec3 c0 = position + dpdx + dpdy;
		vec3 c1 = position - dpdx - dpdy;
		AABB aabb(min(c0, c1), max(c0, c1));
		if (!SIMD::frustum_cull(aabb, base_context->get_visibility_frustum().get_planes()))
			return false;

		// Only render if we are above the plane.
		float plane_test = dot(base_context->get_render_parameters().camera_position - position, normal);
		return plane_test > 0.0f;
	});
#endif

	lighting.set_build_render_pass([this, type](Vulkan::CommandBuffer &cmd) {
		if (type == Reflection)
		{
			mat4 proj, view;
			float z_near;
			compute_plane_reflection(proj, view, base_context->get_render_parameters().camera_position, position, normal, up,
			                         rad_up, rad_x, z_near, zfar);

			// FIXME: Should not be allowed.
			LOGE("FIXME, TexturePlane::add_render_pass\n");
			//renderer.set_mesh_renderer_options(Renderer::ENVIRONMENT_ENABLE_BIT | Renderer::SHADOW_ENABLE_BIT);

			if (zfar > z_near)
				render_main_pass(cmd, proj, view);
		}
		else if (type == Refraction)
		{
			mat4 proj, view;
			float z_near;
			compute_plane_refraction(proj, view, base_context->get_render_parameters().camera_position, position, normal, up,
			                         rad_up, rad_x, z_near, zfar);

			// FIXME: Should not be allowed.
			//renderer.set_mesh_renderer_options(Renderer::ENVIRONMENT_ENABLE_BIT | Renderer::SHADOW_ENABLE_BIT | Renderer::REFRACTION_ENABLE_BIT);

			if (zfar > z_near)
				render_main_pass(cmd, proj, view);
		}
	});

	lighting.add_texture_input("shadow-main");

	auto &reflection_blur_pass = graph.add_pass(name, RENDER_GRAPH_QUEUE_GRAPHICS_BIT);
	auto &reflection_input_res = reflection_blur_pass.add_texture_input(name + "-HDR");
	reflection_blur_pass.add_color_output(name, reflection_blur);
	reflection_blur_pass.set_build_render_pass([&](Vulkan::CommandBuffer &cmd) {
		cmd.set_texture(0, 0, graph.get_physical_texture_resource(reflection_input_res), Vulkan::StockSampler::LinearClamp);
		CommandBufferUtil::draw_fullscreen_quad(cmd, "builtin://shaders/quad.vert", "builtin://shaders/blur.frag",
		                                        {{"METHOD", 6}});
	});
}

void TexturePlane::add_render_passes(RenderGraph &graph)
{
	if (need_reflection)
		add_render_pass(graph, Reflection);
	if (need_refraction)
		add_render_pass(graph, Refraction);
}

void TexturePlane::set_base_renderer(const RendererSuite *suite)
{
	renderer_suite = suite;
}

void TexturePlane::set_base_render_context(const RenderContext *context_)
{
	base_context = context_;
}

void TexturePlane::get_render_info(const RenderContext &context_, const RenderInfoComponent *,
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
	info.push.base_emissive = vec4(base_emissive, 0.0f);

	Hasher h;
	if (info.reflection)
		h.u64(info.reflection->get_cookie());
	else
		h.u32(0);

	if (info.refraction)
		h.u64(info.refraction->get_cookie());
	else
		h.u32(0);

	h.u64(info.normal->get_cookie());
	auto instance_key = h.get();
	auto sorting_key = RenderInfo::get_sort_key(context_, Queue::OpaqueEmissive, h.get(), h.get(), position);
	auto *plane_info = queue.push<TexturePlaneInfo>(Queue::OpaqueEmissive, instance_key, sorting_key,
	                                                texture_plane_render, nullptr);

	if (plane_info)
	{
		unsigned mat_mask = MATERIAL_EMISSIVE_BIT;
		mat_mask |= info.refraction ? MATERIAL_EMISSIVE_REFRACTION_BIT : 0;
		mat_mask |= info.reflection ? MATERIAL_EMISSIVE_REFLECTION_BIT : 0;
		info.program = queue.get_shader_suites()[ecast(RenderableType::TexturePlane)].get_program(DrawPipeline::Opaque, 0, mat_mask);
		*plane_info = info;
	}
}

}
