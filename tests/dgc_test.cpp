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

	void on_device_created(const DeviceCreatedEvent &e)
	{
		VkIndirectCommandsLayoutCreateInfoNV info = { VK_STRUCTURE_TYPE_INDIRECT_COMMANDS_LAYOUT_CREATE_INFO_NV };

		struct DGC
		{
			VkBindShaderGroupIndirectCommandNV shader;
			VkDrawIndirectCommand draw;
		};

		info.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		const uint32_t stride = sizeof(DGC);
		info.pStreamStrides = &stride;
		info.streamCount = 1;

		VkIndirectCommandsLayoutTokenNV tokens[2] = {};

		tokens[0].sType = VK_STRUCTURE_TYPE_INDIRECT_COMMANDS_LAYOUT_TOKEN_NV;
		tokens[0].tokenType = VK_INDIRECT_COMMANDS_TOKEN_TYPE_SHADER_GROUP_NV;
		tokens[0].offset = offsetof(DGC, shader);
		tokens[1].sType = VK_STRUCTURE_TYPE_INDIRECT_COMMANDS_LAYOUT_TOKEN_NV;
		tokens[1].tokenType = VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_NV;
		tokens[1].offset = offsetof(DGC, draw);
		info.pTokens = tokens;
		info.tokenCount = 2;

		auto &table = e.get_device().get_device_table();
		if (table.vkCreateIndirectCommandsLayoutNV(e.get_device().get_device(), &info,
												   nullptr, &indirect_layout) != VK_SUCCESS)
		{
			LOGI("Failed to create layout.\n");
			return;
		}

		static const DGC dgc_data[] = {
			{ { 0 }, { 3, 1, 0, 0 } },
			{ { 1 }, { 3, 1, 0, 0 } },
			{ { 2 }, { 3, 1, 0, 0 } },
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

		e.get_device().wait_idle();
		e.get_device().get_device_table().vkDestroyIndirectCommandsLayoutNV(
				e.get_device().get_device(), indirect_layout, nullptr);
		indirect_layout = VK_NULL_HANDLE;
	}

	void render_frame(double, double elapsed_time) override
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

		vec2 vertices[] = {
			vec2(-0.5f, -0.5f),
			vec2(-0.5f, +0.5f),
			vec2(+0.5f, -0.5f),
		};

		auto c = float(muglm::cos(elapsed_time * 2.0));
		auto s = float(muglm::sin(elapsed_time * 2.0));
		mat2 m{vec2(c, -s), vec2(s, c)};
		for (auto &v : vertices)
			v = m * v;

		auto *verts = static_cast<vec2 *>(cmd->allocate_vertex_data(0, sizeof(vertices), sizeof(vec2)));
		memcpy(verts, vertices, sizeof(vertices));
		cmd->set_vertex_attrib(0, 0, VK_FORMAT_R32G32_SFLOAT, 0);

		auto &table = device.get_device_table();

		{
			// TODO: automate this.
			VkGeneratedCommandsMemoryRequirementsInfoNV generated =
					{ VK_STRUCTURE_TYPE_GENERATED_COMMANDS_MEMORY_REQUIREMENTS_INFO_NV };
			VkMemoryRequirements2 reqs = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2 };

			generated.pipeline = cmd->get_current_graphics_pipeline();
			generated.pipelineBindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
			generated.indirectCommandsLayout = indirect_layout;
			generated.maxSequencesCount = 3;

			table.vkGetGeneratedCommandsMemoryRequirementsNV(device.get_device(), &generated, &reqs);

			BufferCreateInfo bufinfo = {};
			bufinfo.size = reqs.memoryRequirements.size;
			bufinfo.domain = BufferDomain::Device;
			bufinfo.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
			bufinfo.allocation_requirements = reqs.memoryRequirements;
			auto preprocess_buffer = device.create_buffer(bufinfo);

			VkIndirectCommandsStreamNV stream = {};
			stream.buffer = dgc_buffer->get_buffer();
			stream.offset = 0;

			VkGeneratedCommandsInfoNV exec_info = { VK_STRUCTURE_TYPE_GENERATED_COMMANDS_INFO_NV };
			exec_info.indirectCommandsLayout = indirect_layout;
			exec_info.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			exec_info.streamCount = 1;
			exec_info.pStreams = &stream;
			exec_info.preprocessSize = preprocess_buffer->get_create_info().size;
			exec_info.preprocessBuffer = preprocess_buffer->get_buffer();
			exec_info.sequencesCount = 3;
			exec_info.pipeline = cmd->get_current_graphics_pipeline();
			device.get_device_table().vkCmdExecuteGeneratedCommandsNV(cmd->get_command_buffer(), VK_FALSE, &exec_info);
		}

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