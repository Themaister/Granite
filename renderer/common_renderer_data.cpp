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

#include "common_renderer_data.hpp"
#include "mesh_util.hpp"
#include "muglm/muglm_impl.hpp"
#include <random>

namespace Granite
{
PersistentFrameEvent::PersistentFrameEvent()
{
	EVENT_MANAGER_REGISTER(PersistentFrameEvent, on_frame_time, FrameTickEvent);
}

bool PersistentFrameEvent::on_frame_time(const FrameTickEvent &tick)
{
	frame_time = float(tick.get_frame_time());
	return true;
}

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

SSAOLookupTables::SSAOLookupTables()
{
	EVENT_MANAGER_REGISTER_LATCH(SSAOLookupTables, on_device_created, on_device_destroyed, Vulkan::DeviceCreatedEvent);
}

void SSAOLookupTables::on_device_created(const Vulkan::DeviceCreatedEvent &e)
{
	auto &device = e.get_device();

	// Reused from http://john-chapman-graphics.blogspot.com/2013/01/ssao-tutorial.html.

	std::mt19937 rnd;
	std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
	std::uniform_real_distribution<float> dist_u(0.0f, 1.0f);

	Vulkan::ImageCreateInfo info = Vulkan::ImageCreateInfo::immutable_2d_image(4, 4, VK_FORMAT_R16G16_SFLOAT);
	noise_resolution = 4;

	vec2 noise_samples[4 * 4];
	for (auto &n : noise_samples)
	{
		float x = dist(rnd);
		float y = dist(rnd);
		n = normalize(vec2(x, y));
	}

	u16vec2 noise_samples_fp16[4 * 4];
	for (unsigned i = 0; i < 4 * 4; i++)
		noise_samples_fp16[i] = floatToHalf(noise_samples[i]);

	Vulkan::ImageInitialData initial = { noise_samples_fp16, 0, 0 };
	noise = device.create_image(info, &initial);

	static const unsigned SSAO_KERNEL_SIZE = 16;
	kernel_size = SSAO_KERNEL_SIZE;
	vec4 hemisphere[SSAO_KERNEL_SIZE];
	for (unsigned i = 0; i < SSAO_KERNEL_SIZE; i++)
	{
		float x = dist(rnd);
		float y = dist(rnd);
		float z = dist_u(rnd);
		hemisphere[i] = vec4(normalize(vec3(x, y, z)), 0.0f);

		float scale = float(i) / float(kernel_size);
		scale = mix(0.1f, 1.0f, scale * scale);
		hemisphere[i] *= scale;
	}

	Vulkan::BufferCreateInfo buffer = {};
	buffer.size = sizeof(hemisphere);
	buffer.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	buffer.domain = Vulkan::BufferDomain::Device;
	kernel = device.create_buffer(buffer, hemisphere);
}

void SSAOLookupTables::on_device_destroyed(const Vulkan::DeviceCreatedEvent &)
{
	kernel.reset();
	noise.reset();
}

BRDFTables::BRDFTables()
{
	EVENT_MANAGER_REGISTER_LATCH(BRDFTables, on_device_created, on_device_destroyed, Vulkan::DeviceCreatedEvent);
}

void BRDFTables::on_device_created(const Vulkan::DeviceCreatedEvent &e)
{
	texture = e.get_device().get_texture_manager().request_texture("builtin://textures/ibl_brdf_lut.gtx");
}

void BRDFTables::on_device_destroyed(const Vulkan::DeviceCreatedEvent &)
{
	texture = nullptr;
}

Vulkan::Texture *BRDFTables::get_texture() const
{
	return texture;
}
}
