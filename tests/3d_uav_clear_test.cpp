#include "device.hpp"
#include "context.hpp"
#include "application.hpp"

using namespace Vulkan;
using namespace Granite;

struct BasicComputeTest : Granite::Application, Granite::EventHandler
{
	BasicComputeTest()
	{
		EVENT_MANAGER_REGISTER_LATCH(BasicComputeTest, on_device_create, on_device_destroy, DeviceCreatedEvent);
	}

	void on_device_create(const DeviceCreatedEvent &e)
	{
		ImageCreateInfo img = ImageCreateInfo::immutable_3d_image(1536, 384, 24, VK_FORMAT_R16G16B16A16_SFLOAT);
		img.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		image = e.get_device().create_image(img);
		image->set_layout(Layout::General);
	}

	void on_device_destroy(const DeviceCreatedEvent &)
	{
		image.reset();
	}

	void render_frame(double, double) override
	{
		auto &device = get_wsi().get_device();
		auto cmd = device.request_command_buffer();

		cmd->image_barrier(*image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
						   VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_WRITE_BIT,
						   VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT);

		cmd->begin_region("back-to-back-clear");
		for (unsigned i = 0; i < 8; i++)
		{
			cmd->clear_image(*image, {});
			cmd->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
			             VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT);
		}
		cmd->end_region();

		cmd->begin_region("back-to-back-clear-full-memory-barrier");
		for (unsigned i = 0; i < 8; i++)
		{
			cmd->clear_image(*image, {});
			cmd->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_MEMORY_WRITE_BIT,
			             VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT);
		}
		cmd->end_region();

		cmd->image_barrier(*image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
		                   VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_WRITE_BIT,
		                   VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT);

		cmd->set_program("assets://shaders/clear-uav.comp");

		cmd->begin_region("back-to-back-uav-clear");
		for (unsigned i = 0; i < 8; i++)
		{
			cmd->set_storage_texture(0, 0, image->get_view());
			cmd->dispatch(image->get_width() / 4, image->get_height() / 4, image->get_depth() / 4);
			cmd->barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
			             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
						 VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
		}
		cmd->end_region();

		cmd->begin_region("back-to-back-uav-clear-full-memory-barrier");
		for (unsigned i = 0; i < 8; i++)
		{
			cmd->set_storage_texture(0, 0, image->get_view());
			cmd->dispatch(image->get_width() / 4, image->get_height() / 4, image->get_depth() / 4);
			cmd->barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_MEMORY_WRITE_BIT,
			             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			             VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT);
		}
		cmd->end_region();

		auto rp = device.get_swapchain_render_pass(SwapchainRenderPass::ColorOnly);
		rp.clear_color[0].float32[1] = 1.0f;
		rp.clear_color[0].float32[2] = 1.0f;
		cmd->begin_render_pass(rp);
		cmd->end_render_pass();
		device.submit(cmd);
	}

	ImageHandle image;
};

namespace Granite
{
Application *application_create(int, char **)
{
	GRANITE_APPLICATION_SETUP_FILESYSTEM();

	try
	{
		auto *app = new BasicComputeTest();
		return app;
	}
	catch (const std::exception &e)
	{
		LOGE("application_create() threw exception: %s\n", e.what());
		return nullptr;
	}
}
} // namespace Granite