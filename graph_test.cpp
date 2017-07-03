#include "render_graph.hpp"
#include "application.hpp"

using namespace Granite;

class RPImpl : public RenderPassImplementation
{
public:
	void build_render_pass(RenderPass &, Vulkan::CommandBuffer &) override
	{
	}

	bool get_clear_color(unsigned index, VkClearColorValue *value) override
	{
		if (value)
		{
			value->float32[0] = 1.0f;
			value->float32[1] = 0.1f;
			value->float32[2] = 0.1f;
			value->float32[3] = 1.0f;
		}

		return true;
	}

	bool get_clear_depth_stencil(VkClearDepthStencilValue *value) override
	{
		if (value)
		{
			value->depth = 1.0f;
			value->stencil = 0;
		}

		return true;
	}
};

class RenderGraphTest : public Application, public EventHandler
{
public:
	RenderGraphTest()
		: Application(1280, 720)
	{
		EventManager::get_global().register_latch_handler(Vulkan::SwapchainParameterEvent::type_id,
		                                                  &RenderGraphTest::on_swapchain_created,
		                                                  &RenderGraphTest::on_swapchain_destroyed,
		                                                  this);
	}

	void bake_graph(const Vulkan::SwapchainParameterEvent &parameter)
	{
		graph.reset();

		AttachmentInfo smol;
		smol.size_x = 0.5f;
		smol.size_y = 0.5f;

		AttachmentInfo info;
		info.size_x = 2.0f;
		info.size_y = 2.0f;
		auto ds_info = info;
		ds_info.format = get_wsi().get_device().get_default_depth_format();

		ResourceDimensions dim;
		dim.width = parameter.get_width();
		dim.height = parameter.get_height();
		dim.format = parameter.get_format();
		graph.set_backbuffer_dimensions(dim);

		auto &smol_pass = graph.add_pass("smol");
		smol_pass.add_color_output("input", smol);
		smol_pass.set_implementation(&clear_screen);

		auto &pass = graph.add_pass("pass");
		pass.add_color_output("screen", info);
		pass.add_color_input("input");
		pass.set_depth_stencil_output("depth", ds_info);
		pass.set_implementation(&clear_screen);

		graph.set_backbuffer_source("screen");

		graph.bake();
		graph.log();
	}

	void render_frame(double, double) override
	{
		auto &device = get_wsi().get_device();
		graph.setup_attachments(device, &device.get_swapchain_view());
		auto cmd = device.request_command_buffer();
		graph.enqueue_initial_barriers(*cmd);
		graph.enqueue_render_passes(*cmd);
		device.submit(cmd);
	}

private:
	RenderGraph graph;
	RPImpl clear_screen;

	void on_swapchain_created(const Event &event)
	{
		bake_graph(event.as<Vulkan::SwapchainParameterEvent>());
	}

	void on_swapchain_destroyed(const Event &)
	{
	}
};

int Granite::application_main(int, char **)
{
	RenderGraphTest app;
	return app.run();
}