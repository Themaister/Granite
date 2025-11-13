/* Copyright (c) 2017-2025 Hans-Kristian Arntzen
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
#include "application.hpp"
#include "device.hpp"
#include "event_manager.hpp"
#include "event.hpp"

using namespace Granite;
using namespace Vulkan;
using namespace Util;

struct RayQueryApplication : Application, EventHandler
{
	RayQueryApplication()
	{
		EVENT_MANAGER_REGISTER_LATCH(RayQueryApplication, on_device_create, on_device_destroy, DeviceCreatedEvent);
	}

	RTASHandle blas;
	RTASHandle tlas;
	ImageHandle img;
	QueryPoolHandle compaction;

	void on_device_create(const DeviceCreatedEvent &e)
	{
		if (!e.get_device().get_device_features().ray_query_features.rayQuery)
			return;

		auto info = ImageCreateInfo::immutable_2d_image(512, 512, VK_FORMAT_R8G8B8A8_UNORM);
		info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		info.initial_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		img = e.get_device().create_image(info);

		vec3 vbo_data[3] = {
			{ -1, -1, 0 },
			{ +1, -1, 0 },
			{ 0, +1, 0 },
		};

		vec4 transform_data[3] = {
			vec4(1, 0, 0, 3),
			vec4(0, 1, 0, 0),
			vec4(0, 0, 1, 0),
		};

		BufferCreateInfo vbo_info = {};
		vbo_info.size = sizeof(vbo_data);
		vbo_info.domain = BufferDomain::Device;
		vbo_info.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
		auto vbo = e.get_device().create_buffer(vbo_info, vbo_data);

		BufferCreateInfo transform_info = {};
		transform_info.size = sizeof(transform_data);
		transform_info.domain = BufferDomain::Device;
		transform_info.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
		auto transform = e.get_device().create_buffer(transform_info, transform_data);

		BottomRTASGeometry geoms[2] = {};
		geoms[0].vbo = vbo->get_device_address();
		geoms[0].stride = sizeof(vec3);
		geoms[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		geoms[0].num_vertices = 3;
		geoms[0].num_primitives = 1;
		geoms[0].index_type = VK_INDEX_TYPE_NONE_KHR;

		geoms[1].vbo = vbo->get_device_address();
		geoms[1].stride = sizeof(vec3);
		geoms[1].format = VK_FORMAT_R32G32B32_SFLOAT;
		geoms[1].transform = transform->get_device_address();
		geoms[1].num_vertices = 3;
		geoms[1].num_primitives = 1;
		geoms[1].index_type = VK_INDEX_TYPE_NONE_KHR;

		BottomRTASCreateInfo bottom_info = {};
		bottom_info.mode = BLASMode::Static;
		bottom_info.count = 2;
		bottom_info.geometries = geoms;

		auto cmd = e.get_device().request_command_buffer(CommandBuffer::Type::AsyncCompute);
		cmd->begin_rtas_batch();
		blas = e.get_device().create_rtas(bottom_info, cmd.get(), &compaction);
		cmd->end_rtas_batch();

		VkAccelerationStructureInstanceKHR instances[2] = {};

		instances[0].mask = 0xff;
		instances[0].accelerationStructureReference = blas->get_device_address();
		instances[0].transform.matrix[0][0] = 1.0f;
		instances[0].transform.matrix[1][1] = 1.0f;
		instances[0].transform.matrix[2][2] = 1.0f;
		instances[0].transform.matrix[1][3] = -2.0f;

		instances[1].mask = 0xff;
		instances[1].accelerationStructureReference = blas->get_device_address();
		instances[1].transform.matrix[0][0] = 1.0f;
		instances[1].transform.matrix[1][1] = 1.0f;
		instances[1].transform.matrix[2][2] = 1.0f;
		instances[1].transform.matrix[1][3] = +2.0f;

		RTASInstance inst[2] = {};
		inst[0].instance = &instances[0];
		inst[1].instance = &instances[1];

		TopRTASCreateInfo top_info = {};
		top_info.count = 2;
		top_info.instances = inst;

		cmd->barrier(VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
		             VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
		             VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
		             VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR);

		cmd->begin_rtas_batch();
		e.get_device().create_rtas(top_info, cmd.get());
		cmd->end_rtas_batch();

		cmd->barrier(VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
		             VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
		             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		             VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR);

		e.get_device().submit(cmd);
	}

	void on_device_destroy(const DeviceCreatedEvent &)
	{
		blas.reset();
		tlas.reset();
		img.reset();
	}

	void render_frame(double, double) override
	{
		auto &device = get_wsi().get_device();

		if (!device.get_device_features().ray_query_features.rayQuery)
		{
			request_shutdown();
			return;
		}

		auto cmd = device.request_command_buffer();

		cmd->begin_render_pass(device.get_swapchain_render_pass(SwapchainRenderPass::ColorOnly));
		cmd->end_render_pass();
		device.submit(cmd);
	}
};

namespace Granite
{
Application *application_create(int argc, char **argv)
{
	GRANITE_APPLICATION_SETUP_FILESYSTEM();

	try
	{
		auto *app = new RayQueryApplication();
		return app;
	}
	catch (const std::exception &e)
	{
		LOGE("application_create() threw exception: %s\n", e.what());
		return nullptr;
	}
}
}
