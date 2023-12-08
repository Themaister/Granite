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
using namespace Vulkan::Meshlet;

static uint32_t style_to_u32_streams(MeshStyle style)
{
	switch (style)
	{
	case MeshStyle::Wireframe:
		return 3;
	case MeshStyle::Untextured:
		return 4;
	case MeshStyle::Textured:
		return 7;
	case MeshStyle::Skinned:
		return 9;
	default:
		return 0;
	}
}

struct MeshletViewerApplication : Granite::Application, Granite::EventHandler
{
	MeshletViewerApplication(const char *path)
	{
		get_wsi().set_backbuffer_srgb(false);
		mesh_id = GRANITE_ASSET_MANAGER()->register_asset(*GRANITE_FILESYSTEM(), path, Granite::AssetClass::Mesh);
		EVENT_MANAGER_REGISTER_LATCH(MeshletViewerApplication, on_device_create, on_device_destroy, DeviceCreatedEvent);
	}

	AABB aabb;
	FPSCamera camera;
	Granite::AssetID mesh_id;

	void on_device_create(const DeviceCreatedEvent &e)
	{
		e.get_device().get_shader_manager().add_include_directory("builtin://shaders/inc");
	}

	void on_device_destroy(const DeviceCreatedEvent &)
	{
	}

	void render_frame(double, double) override
	{
		auto &wsi = get_wsi();
		auto &device = wsi.get_device();
		auto cmd = device.request_command_buffer();

		cmd->begin_render_pass(device.get_swapchain_render_pass(SwapchainRenderPass::Depth));
		camera.set_aspect(cmd->get_viewport().width / cmd->get_viewport().height);

		cmd->set_opaque_state();

		auto vp = camera.get_projection() * camera.get_view();
		*cmd->allocate_typed_constant_data<mat4>(1, 0, 1) = vp;
		auto draw = device.get_resource_manager().get_mesh_draw_range(mesh_id);

		if (draw.count && device.get_resource_manager().get_mesh_encoding() == Vulkan::ResourceManager::MeshEncoding::Meshlet)
		{
			bool large_workgroup =
					device.get_device_features().mesh_shader_properties.maxPreferredMeshWorkGroupInvocations > 32 &&
					device.get_device_features().mesh_shader_properties.maxMeshWorkGroupInvocations >= 256;

			cmd->set_program("", "assets://shaders/meshlet_debug.mesh",
			                 "assets://shaders/meshlet_debug.mesh.frag",
			                 {{"MESHLET_PAYLOAD_LARGE_WORKGROUP", int(large_workgroup)}});

			cmd->set_storage_buffer(0, 0, *device.get_resource_manager().get_meshlet_header_buffer());
			cmd->set_storage_buffer(0, 1, *device.get_resource_manager().get_meshlet_stream_header_buffer());
			cmd->set_storage_buffer(0, 2, *device.get_resource_manager().get_meshlet_payload_buffer());

			cmd->enable_subgroup_size_control(true, VK_SHADER_STAGE_MESH_BIT_EXT);
			cmd->set_subgroup_size_log2(true, 5, 5, VK_SHADER_STAGE_MESH_BIT_EXT);
			cmd->set_specialization_constant_mask(1);
			cmd->set_specialization_constant(0, style_to_u32_streams(draw.style));

			cmd->push_constants(&draw.offset, 0, sizeof(draw.offset));
			cmd->draw_mesh_tasks(draw.count, 1, 1);
		}
		else if (draw.count)
		{
			auto *ibo = device.get_resource_manager().get_index_buffer();
			auto *pos = device.get_resource_manager().get_position_buffer();
			auto *attr = device.get_resource_manager().get_attribute_buffer();
			auto *indirect = device.get_resource_manager().get_indirect_buffer();

			cmd->set_program("assets://shaders/meshlet_debug.vert", "assets://shaders/meshlet_debug.frag");
			cmd->set_index_buffer(*ibo, 0, VK_INDEX_TYPE_UINT8_EXT);
			cmd->set_vertex_binding(0, *pos, 0, 12);
			cmd->set_vertex_binding(1, *attr, 0, 16);
			cmd->set_vertex_attrib(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0);
			cmd->set_vertex_attrib(1, 1, VK_FORMAT_A2B10G10R10_SNORM_PACK32, 0);
			cmd->set_vertex_attrib(2, 1, VK_FORMAT_A2B10G10R10_SNORM_PACK32, 4);
			cmd->set_vertex_attrib(3, 1, VK_FORMAT_R32G32_SFLOAT, 8);
			cmd->draw_indexed_indirect(*indirect,
			                           draw.offset * sizeof(VkDrawIndexedIndirectCommand),
			                           draw.count, sizeof(VkDrawIndexedIndirectCommand));
		}

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
