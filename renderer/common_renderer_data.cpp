/* Copyright (c) 2017-2024 Hans-Kristian Arntzen
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

#include "common_renderer_data.hpp"
#include "mesh_util.hpp"
#include "muglm/muglm_impl.hpp"
#include <random>

namespace Granite
{
LightMesh::LightMesh()
{
	EVENT_MANAGER_REGISTER_LATCH(LightMesh, on_device_created, on_device_destroyed, Vulkan::DeviceCreatedEvent);
}

void LightMesh::create_point_mesh(const Vulkan::DeviceCreatedEvent &e)
{
	auto mesh = create_sphere_mesh(3);

	Vulkan::BufferCreateInfo info = {};
	info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	info.size = mesh.positions.size() * sizeof(mesh.positions[0]);
	info.domain = Vulkan::BufferDomain::Device;
	point_vbo = e.get_device().create_buffer(info, mesh.positions.data());

	info.size = mesh.indices.size() * sizeof(uint16_t);
	info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	point_ibo = e.get_device().create_buffer(info, mesh.indices.data());

	point_count = mesh.indices.size();
}

void LightMesh::create_spot_mesh(const Vulkan::DeviceCreatedEvent &e)
{
	vec3 positions[16 + 2];
	positions[0] = vec3(0.0f);
	positions[1] = vec3(0.0f, 0.0f, -1.0f);

	float half_angle = 2.0f * pi<float>() / 32.0f;
	float padding_mod = 1.0f / cos(half_angle);

	// Make sure top and bottom are aligned to 1 so we can get correct culling checks,
	// rotate the mesh by half phase so we get a "flat" top and "flat" side.
	for (unsigned i = 0; i < 16; i++)
	{
		float rad = 2.0f * pi<float>() * (i + 0.5f) / 16.0f;
		positions[i + 2] = vec3(padding_mod * cos(rad), padding_mod * sin(rad), -1.0f);
	}

	std::vector <uint16_t> indices;
	indices.reserve(2 * 3 * 16);
	for (unsigned i = 0; i < 16; i++)
	{
		indices.push_back(0);
		indices.push_back((i & 15) + 2);
		indices.push_back(((i + 1) & 15) + 2);
	}

	for (unsigned i = 0; i < 16; i++)
	{
		indices.push_back(1);
		indices.push_back(((i + 1) & 15) + 2);
		indices.push_back((i & 15) + 2);
	}

	spot_count = indices.size();

	Vulkan::BufferCreateInfo info = {};
	info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	info.size = sizeof(positions);
	info.domain = Vulkan::BufferDomain::Device;
	spot_vbo = e.get_device().create_buffer(info, positions);

	info.size = indices.size() * sizeof(uint16_t);
	info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	spot_ibo = e.get_device().create_buffer(info, indices.data());
}

void LightMesh::on_device_created(const Vulkan::DeviceCreatedEvent &e)
{
	create_spot_mesh(e);
	create_point_mesh(e);
}

void LightMesh::on_device_destroyed(const Vulkan::DeviceCreatedEvent &)
{
	spot_vbo.reset();
	spot_ibo.reset();
	point_vbo.reset();
	point_ibo.reset();
}

void CommonRendererData::initialize_static_assets(AssetManager *iface, Filesystem *fs)
{
	LOGI("Initializing static assets.\n");
	brdf_tables = iface->register_asset(*fs, "builtin://textures/ibl_brdf_lut.gtx", AssetClass::ImageZeroable,
	                                    AssetManager::persistent_prio());
}
}
