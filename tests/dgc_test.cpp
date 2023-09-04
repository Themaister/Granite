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
	Vulkan::BufferHandle ssbo;

	void on_device_created(const DeviceCreatedEvent &e)
	{
		struct DGC
		{
			uint32_t push;
			VkDispatchIndirectCommand dispatch;
		};

		IndirectLayoutToken tokens[2] = {};

		{
			BufferCreateInfo buf_info = {};
			buf_info.domain = BufferDomain::CachedHost;
			buf_info.size = 64 * sizeof(uint32_t);
			buf_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
			ssbo = e.get_device().create_buffer(buf_info, nullptr);
		}

		auto *layout = e.get_device().get_shader_manager().register_compute("assets://shaders/dgc_compute.comp")->
				register_variant({})->get_program()->get_pipeline_layout();

		tokens[0].type = IndirectLayoutToken::Type::PushConstant;
		tokens[0].offset = offsetof(DGC, push);
		tokens[0].data.push.range = 8;
		tokens[0].data.push.offset = 0;
		tokens[0].data.push.layout = layout;
		tokens[1].type = IndirectLayoutToken::Type::Dispatch;
		tokens[1].offset = offsetof(DGC, dispatch);

		indirect_layout = e.get_device().request_indirect_layout(tokens, 2, sizeof(DGC));

		static const DGC dgc_data[] = {
			{ 0, { 100, 200, 30 } },
			{ 1, { 300, 200, 30 } },
			{ 2, { 500, 200, 30 } },
			{ 3, { 600, 200, 30 } },
		};

		BufferCreateInfo buf_info = {};
		buf_info.domain = BufferDomain::LinkedDeviceHost;
		buf_info.size = sizeof(dgc_data);
		buf_info.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
		dgc_buffer = e.get_device().create_buffer(buf_info, dgc_data);
	}

	void on_device_destroyed(const DeviceCreatedEvent &)
	{
		dgc_buffer.reset();
		ssbo.reset();
		indirect_layout = nullptr;
	}

	void render_frame(double, double) override
	{
		auto &wsi = get_wsi();
		auto &device = wsi.get_device();

		auto cmd = device.request_command_buffer();

		cmd->set_storage_buffer(0, 0, *ssbo);
		cmd->set_program("assets://shaders/dgc_compute.comp");
		cmd->execute_indirect_commands(indirect_layout, 1, *dgc_buffer, 0, nullptr, 0);
		//cmd->execute_indirect_commands(indirect_layout, 2, *dgc_buffer, 0, nullptr, 0);
		//cmd->execute_indirect_commands(indirect_layout, 3, *dgc_buffer, 0, nullptr, 0);
		//cmd->execute_indirect_commands(indirect_layout, 4, *dgc_buffer, 0, nullptr, 0);

		cmd->begin_render_pass(device.get_swapchain_render_pass(SwapchainRenderPass::ColorOnly));
		cmd->end_render_pass();

		Fence fence;
		device.submit(cmd, &fence);
		fence->wait();
		auto *ptr = static_cast<const uint32_t *>(device.map_host_buffer(*ssbo, MEMORY_ACCESS_READ_BIT));
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