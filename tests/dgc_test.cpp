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

	const IndirectLayout *indirect_layout = nullptr;
	Vulkan::BufferHandle dgc_buffer;
	Vulkan::BufferHandle dgc_count_buffer;
	Vulkan::BufferHandle ssbo;
	Vulkan::BufferHandle ssbo_readback;
	Vulkan::ImageHandle image;

	void on_device_created(const DeviceCreatedEvent &e)
	{
		struct DGC
		{
			uint32_t push;
			VkDrawIndirectCommand draw;
		};

		IndirectLayoutToken tokens[2] = {};

		{
			BufferCreateInfo buf_info = {};
			buf_info.domain = BufferDomain::Device;
			buf_info.size = 64 * sizeof(uint32_t);
			buf_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
			ssbo = e.get_device().create_buffer(buf_info, nullptr);
			buf_info.domain = BufferDomain::CachedHost;
			ssbo_readback = e.get_device().create_buffer(buf_info, nullptr);
		}

		auto *layout = e.get_device().get_shader_manager().register_graphics(
				"assets://shaders/dgc.vert", "assets://shaders/dgc.frag")->
				register_variant({})->get_program()->get_pipeline_layout();

		tokens[0].type = IndirectLayoutToken::Type::PushConstant;
		tokens[0].offset = offsetof(DGC, push);
		tokens[0].data.push.range = 4;
		tokens[0].data.push.offset = 0;
		tokens[0].data.push.layout = layout;
		tokens[1].type = IndirectLayoutToken::Type::Draw;
		tokens[1].offset = offsetof(DGC, draw);

		indirect_layout = e.get_device().request_indirect_layout(tokens, 2, sizeof(DGC));

		static const DGC dgc_data[] = {
			{ 0, { 3 * 1000000, 1 } },
			{ 1, { 3 * 2000000, 1 } },
			{ 2, { 3 * 3000000, 1 } },
			{ 3, { 3 * 4000000, 1 } },
		};

		BufferCreateInfo buf_info = {};
		buf_info.domain = BufferDomain::LinkedDeviceHost;
		buf_info.size = sizeof(dgc_data);
		buf_info.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
		dgc_buffer = e.get_device().create_buffer(buf_info, dgc_data);

		static const uint32_t count_data[] = { 1, 2, 3, 4 };
		buf_info.size = sizeof(count_data);
		dgc_count_buffer = e.get_device().create_buffer(buf_info, count_data);

		{
			auto img = ImageCreateInfo::render_target(64, 64, VK_FORMAT_R8_UNORM);
			img.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
			img.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
			image = e.get_device().create_image(img);
		}
	}

	void on_device_destroyed(const DeviceCreatedEvent &)
	{
		dgc_buffer.reset();
		dgc_count_buffer.reset();
		ssbo.reset();
		ssbo_readback.reset();
		image.reset();
		indirect_layout = nullptr;
	}

	void render_frame(double, double) override
	{
		auto &wsi = get_wsi();
		auto &device = wsi.get_device();

		auto cmd = device.request_command_buffer();

		cmd->image_barrier(*image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
						   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
						   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

		RenderPassInfo rp_info;
		rp_info.num_color_attachments = 1;
		rp_info.color_attachments[0] = &image->get_view();
		rp_info.store_attachments = 1u << 0;
		rp_info.clear_attachments = 1u << 0;

		{
			cmd->begin_render_pass(rp_info);
			cmd->set_storage_buffer(0, 0, *ssbo);
			cmd->set_opaque_state();
			cmd->set_program("assets://shaders/dgc.vert", "assets://shaders/dgc.frag");
			cmd->execute_indirect_commands(indirect_layout, 1, *dgc_buffer, 0, nullptr, 0);
			cmd->end_render_pass();
		}
		rp_info.clear_attachments = 0;

		cmd->barrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
					 VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);

		{
			cmd->begin_render_pass(rp_info);
			cmd->set_storage_buffer(0, 0, *ssbo);
			cmd->set_opaque_state();
			cmd->set_program("assets://shaders/dgc.vert", "assets://shaders/dgc.frag");
			cmd->execute_indirect_commands(indirect_layout, 1, *dgc_buffer, 0, nullptr, 0);
			cmd->end_render_pass();
		}

		cmd->barrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
		             VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);

		{
			cmd->begin_render_pass(rp_info);
			cmd->set_storage_buffer(0, 0, *ssbo);
			cmd->set_opaque_state();
			cmd->set_program("assets://shaders/dgc.vert", "assets://shaders/dgc.frag");
			cmd->execute_indirect_commands(indirect_layout, 1, *dgc_buffer, 0, nullptr, 0);
			cmd->end_render_pass();
		}

		cmd->barrier(VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
					 VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_TRANSFER_READ_BIT);
		cmd->copy_buffer(*ssbo_readback, *ssbo);
		cmd->barrier(VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
					 VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);

		cmd->begin_render_pass(device.get_swapchain_render_pass(SwapchainRenderPass::ColorOnly));
		cmd->end_render_pass();

		Fence fence;
		device.submit(cmd, &fence);
		fence->wait();
		auto *ptr = static_cast<const uint32_t *>(device.map_host_buffer(*ssbo_readback, MEMORY_ACCESS_READ_BIT));
		LOGI("ptr[0] = %u\n", ptr[0]);
		LOGI("ptr[1] = %u\n", ptr[1]);
		LOGI("ptr[2] = %u\n", ptr[2]);
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