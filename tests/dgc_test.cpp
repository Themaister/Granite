#include "application.hpp"
#include "command_buffer.hpp"
#include "device.hpp"
#include "os_filesystem.hpp"
#include "muglm/muglm_impl.hpp"
#include <string.h>

using namespace Granite;
using namespace Vulkan;

struct DGCTriangleApplication : Granite::Application, Granite::EventHandler
{
	DGCTriangleApplication()
	{
		EVENT_MANAGER_REGISTER_LATCH(DGCTriangleApplication, on_device_created, on_device_destroyed, DeviceCreatedEvent);
	}

	VkIndirectCommandsLayoutNV indirect_layout = VK_NULL_HANDLE;
	Vulkan::BufferHandle dgc_buffer;
	Vulkan::BufferHandle vbo;

	void on_device_created(const DeviceCreatedEvent &e)
	{
		VkIndirectCommandsLayoutCreateInfoNV info = { VK_STRUCTURE_TYPE_INDIRECT_COMMANDS_LAYOUT_CREATE_INFO_NV };

		struct DGC
		{
			VkBindShaderGroupIndirectCommandNV shader;
			alignas(8) VkBindVertexBufferIndirectCommandNV vbo;
			VkDrawIndirectCommand draw;
		};

		info.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		const uint32_t stride = sizeof(DGC);
		info.pStreamStrides = &stride;
		info.streamCount = 1;

		VkIndirectCommandsLayoutTokenNV tokens[3] = {};

		tokens[0].sType = VK_STRUCTURE_TYPE_INDIRECT_COMMANDS_LAYOUT_TOKEN_NV;
		tokens[0].tokenType = VK_INDIRECT_COMMANDS_TOKEN_TYPE_SHADER_GROUP_NV;
		tokens[0].offset = offsetof(DGC, shader);
		tokens[1].sType = VK_STRUCTURE_TYPE_INDIRECT_COMMANDS_LAYOUT_TOKEN_NV;
		tokens[1].offset = offsetof(DGC, vbo);
		tokens[1].tokenType = VK_INDIRECT_COMMANDS_TOKEN_TYPE_VERTEX_BUFFER_NV;
		tokens[2].sType = VK_STRUCTURE_TYPE_INDIRECT_COMMANDS_LAYOUT_TOKEN_NV;
		tokens[2].tokenType = VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_NV;
		tokens[2].offset = offsetof(DGC, draw);
		info.pTokens = tokens;
		info.tokenCount = 3;

		auto &table = e.get_device().get_device_table();
		if (table.vkCreateIndirectCommandsLayoutNV(e.get_device().get_device(), &info,
												   nullptr, &indirect_layout) != VK_SUCCESS)
		{
			LOGI("Failed to create layout.\n");
			return;
		}

		const vec2 base_vertices[] = {
			vec2(-0.5f, -0.5f),
			vec2(-0.5f, +0.5f),
			vec2(+0.5f, -0.5f),
		};

		const vec2 offsets[] = {
			vec2(0.5f, 0.5f),
			vec2(-0.5f, -0.5f),
			vec2(-0.5f, 0.5f),
		};

		vec2 vertices[3][3];
		for (unsigned prim = 0; prim < 3; prim++)
			for (unsigned i = 0; i < 3; i++)
				vertices[prim][i] = base_vertices[i] * 0.125f + offsets[prim];

		BufferCreateInfo vbo_info = {};
		vbo_info.size = sizeof(vertices);
		vbo_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		vbo_info.domain = BufferDomain::Device;
		vbo = e.get_device().create_buffer(vbo_info, vertices);

		static const DGC dgc_data[] = {
			{ { 0 }, { vbo->get_device_address(), 3 * sizeof(vec2) }, { 3, 4, 0, 0 } },
			{ { 1 }, { vbo->get_device_address() + 3 * sizeof(vec2), 3 * sizeof(vec2) }, { 3, 4, 0, 0 } },
			{ { 2 }, { vbo->get_device_address() + 2 * 3 * sizeof(vec2), 3 * sizeof(vec2) }, { 3, 4, 0, 0 } },
		};

		BufferCreateInfo buf_info = {};
		buf_info.domain = BufferDomain::LinkedDeviceHost;
		buf_info.size = sizeof(dgc_data);
		buf_info.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
		dgc_buffer = e.get_device().create_buffer(buf_info, dgc_data);
	}

	void on_device_destroyed(const DeviceCreatedEvent &e)
	{
		dgc_buffer.reset();
		vbo.reset();

		e.get_device().wait_idle();
		e.get_device().get_device_table().vkDestroyIndirectCommandsLayoutNV(
				e.get_device().get_device(), indirect_layout, nullptr);
		indirect_layout = VK_NULL_HANDLE;
	}

	void render_frame(double, double) override
	{
		auto &wsi = get_wsi();
		auto &device = wsi.get_device();

		auto cmd = device.request_command_buffer();

		cmd->begin_render_pass(device.get_swapchain_render_pass(SwapchainRenderPass::ColorOnly));
		cmd->set_program("assets://shaders/dgc.vert", "assets://shaders/dgc.frag");
		cmd->set_opaque_state();
		cmd->set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

		auto *base = device.get_shader_manager().register_graphics("assets://shaders/dgc.vert", "assets://shaders/dgc.frag");
		Program * const programs[] = {
			base->register_variant({{ "DGC", 0 }})->get_program(),
			base->register_variant({{ "DGC", 1 }})->get_program(),
			base->register_variant({{ "DGC", 2 }})->get_program(),
		};

		cmd->set_program_group(programs, 3, nullptr);

		cmd->set_vertex_binding(0, *vbo, 0, sizeof(vec2));
		cmd->set_vertex_attrib(0, 0, VK_FORMAT_R32G32_SFLOAT, 0);

		auto *offsets = static_cast<vec2 *>(cmd->allocate_vertex_data(
				1, 4 * sizeof(vec2), sizeof(vec2), VK_VERTEX_INPUT_RATE_INSTANCE));
		offsets[0] = vec2(-0.1f, -0.1f);
		offsets[1] = vec2(+0.1f, -0.1f);
		offsets[2] = vec2(-0.1f, +0.1f);
		offsets[3] = vec2(+0.1f, +0.1f);
		cmd->set_vertex_attrib(1, 1, VK_FORMAT_R32G32_SFLOAT, 0);

		cmd->execute_indirect_commands(indirect_layout, 3, *dgc_buffer, 0, nullptr, 0);
		cmd->end_render_pass();
		device.submit(cmd);
	}
};

namespace Granite
{
Application *application_create(int, char **)
{
	GRANITE_APPLICATION_SETUP_FILESYSTEM();

	try
	{
		auto *app = new DGCTriangleApplication();
		return app;
	}
	catch (const std::exception &e)
	{
		LOGE("application_create() threw exception: %s\n", e.what());
		return nullptr;
	}
}
}