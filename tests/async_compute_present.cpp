#include "application.hpp"
#include "command_buffer.hpp"
#include "device.hpp"
#include "os_filesystem.hpp"
#include "muglm/muglm_impl.hpp"
#include <string.h>

using namespace Granite;
using namespace Vulkan;

struct AsyncComputePresent : Granite::Application, Granite::EventHandler
{
	AsyncComputePresent()
	{
		get_wsi().set_extra_usage_flags(VK_IMAGE_USAGE_STORAGE_BIT);
		get_wsi().set_backbuffer_srgb(false);
		get_wsi().set_support_prerotate(false);
	}

	void render_frame(double, double)
	{
		auto &wsi = get_wsi();
		auto &device = wsi.get_device();

		bool supports_async_present = device.can_touch_swapchain_in_command_buffer(CommandBuffer::Type::AsyncCompute);
		auto cmd = device.request_command_buffer(
				supports_async_present ? CommandBuffer::Type::AsyncCompute : CommandBuffer::Type::Generic);

		if (supports_async_present && (device.get_swapchain_view().get_image().get_create_info().usage & VK_IMAGE_USAGE_STORAGE_BIT))
		{
			auto &image = device.get_swapchain_view().get_image();
			cmd->swapchain_touch_in_stages(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
			cmd->image_barrier(image,
			                   VK_IMAGE_LAYOUT_UNDEFINED,
			                   VK_IMAGE_LAYOUT_GENERAL,
			                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
			                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT);

			cmd->set_program("assets://shaders/write_swapchain.comp");
			struct
			{
				uint32_t width, height;
			} resolution = { image.get_width(), image.get_height() };
			cmd->push_constants(&resolution, 0, sizeof(resolution));
			cmd->set_storage_texture(0, 0, device.get_swapchain_view());
			cmd->dispatch((image.get_width() + 7) / 8, (image.get_height() + 7) / 8, 1);

			cmd->image_barrier(image,
			                   VK_IMAGE_LAYOUT_GENERAL,
			                   VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
			                   VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0);
		}
		else
		{
			auto rp = device.get_swapchain_render_pass(SwapchainRenderPass::ColorOnly);
			rp.clear_color[0].float32[0] = 0.4f;
			rp.clear_color[0].float32[1] = 0.2f;
			rp.clear_color[0].float32[2] = 0.3f;
			cmd->begin_render_pass(rp);
			cmd->end_render_pass();
		}
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
		auto *app = new AsyncComputePresent();
		return app;
	}
	catch (const std::exception &e)
	{
		LOGE("application_create() threw exception: %s\n", e.what());
		return nullptr;
	}
}
}
