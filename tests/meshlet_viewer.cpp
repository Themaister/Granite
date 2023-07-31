/* Copyright (c) 2017-2023 Hans-Kristian Arntzen
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
#include "command_buffer.hpp"
#include "device.hpp"
#include "os_filesystem.hpp"
#include "muglm/muglm_impl.hpp"
#include "meshlet.hpp"
#include "aabb.hpp"
#include "event.hpp"
#include "camera.hpp"
#include "event_manager.hpp"
#include <string.h>
#include <float.h>
#include <stdexcept>

using namespace Granite;
using namespace Vulkan;

struct MeshletViewerApplication : Granite::Application, Granite::EventHandler
{
	MeshletViewerApplication(const char *path)
	{
		get_wsi().set_backbuffer_srgb(false);

		auto file = GRANITE_FILESYSTEM()->open(path, FileMode::ReadOnly);
		if (!file)
			throw std::runtime_error("Failed to open file.");

		mapping = file->map();
		if (!mapping)
			throw std::runtime_error("Failed to map file.");

		EVENT_MANAGER_REGISTER_LATCH(MeshletViewerApplication, on_device_create, on_device_destroy, DeviceCreatedEvent);
	}

	FileMappingHandle mapping;
	Vulkan::BufferHandle ibo;
	Vulkan::BufferHandle vbo;
	Vulkan::BufferHandle payload;
	Vulkan::BufferHandle meshlet_meta_buffer;
	Vulkan::BufferHandle meshlet_stream_buffer;
	AABB aabb;
	FPSCamera camera;

	void on_device_create(const DeviceCreatedEvent &e)
	{
		e.get_device().get_shader_manager().add_include_directory("builtin://shaders/inc");

		auto view = SceneFormats::Meshlet::create_mesh_view(*mapping);
		if (!view.format_header)
			throw std::runtime_error("Failed to load meshlet.");

		Vulkan::BufferCreateInfo info = {};
		info.size = view.total_primitives * sizeof(uvec3);
		info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
		info.domain = Vulkan::BufferDomain::Device;
		ibo = e.get_device().create_buffer(info);

		info.size = view.total_vertices * (view.format_header->u32_stream_count - 1) * sizeof(uint32_t);
		info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		info.domain = Vulkan::BufferDomain::Device;
		vbo = e.get_device().create_buffer(info);

		info.size = view.format_header->payload_size_words * sizeof(uint32_t);
		info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		info.domain = Vulkan::BufferDomain::LinkedDeviceHost;
		payload = e.get_device().create_buffer(info, view.payload);

		auto cmd = e.get_device().request_command_buffer();
		if (!SceneFormats::Meshlet::decode_mesh(*cmd, *ibo, 0, *vbo, 0, *payload, 0, view))
		{
			e.get_device().submit_discard(cmd);
			throw std::runtime_error("Failed to decode mesh.\n");
		}

		cmd->barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
					 VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT);
		e.get_device().submit(cmd);

		aabb = { vec3(FLT_MAX), vec3(FLT_MIN) };
		for (uint32_t i = 0; i < view.format_header->meshlet_count; i++)
		{
			auto cluster_aabb = AABB{
				view.bounds[i].center - view.bounds[i].radius,
				view.bounds[i].center + view.bounds[i].radius,
			};
			aabb.expand(cluster_aabb);
		}

		camera.set_depth_range(0.1f, 200.0f);
		camera.set_fovy(0.4f * pi<float>());
		camera.look_at(aabb.get_center() + vec3(0.1f, 0.2f, 2.1f) * aabb.get_radius(),
		               aabb.get_center(), vec3(0.0f, 1.0f, 0.0f));

		Vulkan::BufferCreateInfo buf_info = {};
		buf_info.domain = Vulkan::BufferDomain::LinkedDeviceHost;
		buf_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

		buf_info.size = view.format_header->meshlet_count * sizeof(*view.headers);
		meshlet_meta_buffer = e.get_device().create_buffer(buf_info, view.headers);

		buf_info.size = view.format_header->meshlet_count * view.format_header->u32_stream_count * sizeof(*view.streams);
		meshlet_stream_buffer = e.get_device().create_buffer(buf_info, view.streams);
	}

	void on_device_destroy(const DeviceCreatedEvent &)
	{
		ibo.reset();
		vbo.reset();
		payload.reset();
		meshlet_meta_buffer.reset();
		meshlet_stream_buffer.reset();
	}

	void render_frame(double, double) override
	{
		auto &wsi = get_wsi();
		auto &device = wsi.get_device();
		auto cmd = device.request_command_buffer();

		cmd->begin_render_pass(device.get_swapchain_render_pass(SwapchainRenderPass::Depth));
		camera.set_aspect(cmd->get_viewport().width / cmd->get_viewport().height);

		cmd->set_program("", "assets://shaders/meshlet_debug.mesh",
		                 "assets://shaders/meshlet_debug.mesh.frag");
		cmd->set_opaque_state();

		auto vp = camera.get_projection() * camera.get_view();
		*cmd->allocate_typed_constant_data<mat4>(1, 0, 1) = vp;

		cmd->set_storage_buffer(0, 0, *meshlet_meta_buffer);
		cmd->set_storage_buffer(0, 1, *meshlet_stream_buffer);
		cmd->set_storage_buffer(0, 2, *payload);

		cmd->enable_subgroup_size_control(true, VK_SHADER_STAGE_MESH_BIT_EXT);
		cmd->set_subgroup_size_log2(true, 5, 5, VK_SHADER_STAGE_MESH_BIT_EXT);
		cmd->draw_mesh_tasks(meshlet_meta_buffer->get_create_info().size / sizeof(SceneFormats::Meshlet::Header), 1, 1);

		cmd->end_render_pass();
		device.submit(cmd);
	}
};

namespace Granite
{
Application *application_create(int argc, char **argv)
{
	GRANITE_APPLICATION_SETUP_FILESYSTEM();

	if (argc != 2)
	{
		LOGE("Usage: meshlet-viewer path.msh1\n");
		return nullptr;
	}

	try
	{
		auto *app = new MeshletViewerApplication(argv[1]);
		return app;
	}
	catch (const std::exception &e)
	{
		LOGE("application_create() threw exception: %s\n", e.what());
		return nullptr;
	}
}
}
