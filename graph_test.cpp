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

class ReadValueImpl : public RenderPassImplementation
{
public:
	void build_render_pass(RenderPass &render_pass, Vulkan::CommandBuffer &cmd) override
	{
		auto &buffer = render_pass.get_graph().get_physical_buffer_resource(render_pass.get_uniform_inputs()[0]->get_physical_index());
		auto &device = cmd.get_device();

		auto *program = device.get_shader_manager().register_graphics("assets://shaders/clear_value.vert", "assets://shaders/clear_value.frag");
		unsigned variant = program->register_variant({});
		cmd.set_program(*program->get_program(variant));
		cmd.set_quad_vertex_state();
		cmd.set_uniform_buffer(0, 0, buffer);
		cmd.set_quad_state();
		cmd.draw(4);
	}
};

class WriteValueImpl : public RenderPassImplementation
{
public:
	void build_render_pass(RenderPass &render_pass, Vulkan::CommandBuffer &cmd) override
	{
		auto &buffer = render_pass.get_graph().get_physical_buffer_resource(render_pass.get_storage_outputs()[0]->get_physical_index());
		auto &device = cmd.get_device();
		auto *program = device.get_shader_manager().register_compute("assets://shaders/write_value.comp");
		unsigned variant = program->register_variant({});
		cmd.set_program(*program->get_program(variant));
		cmd.set_storage_buffer(0, 0, buffer);

		float value = 0.25f;
		cmd.push_constants(&value, 0, sizeof(value));
		cmd.dispatch(1, 1, 1);
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

		BufferInfo buffer_info;
		buffer_info.persistent = true;
		buffer_info.size = 4;
		buffer_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

		auto &compute_pass = graph.add_pass("compute", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
		compute_pass.add_storage_output("constant", buffer_info);
		compute_pass.set_implementation(&write_value);

		auto &compute_pass2 = graph.add_pass("compute2", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
		compute_pass2.add_storage_output("constant2", buffer_info, "constant");
		compute_pass2.set_implementation(&write_value);

		auto &smol_pass = graph.add_pass("smol", VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
		smol_pass.add_color_output("input", smol);
		smol_pass.set_implementation(&clear_screen);

		auto &pass = graph.add_pass("pass", VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
		pass.add_color_output("screen", info, "input");
		pass.set_depth_stencil_output("depth", ds_info);
		pass.add_uniform_input("constant2");
		pass.set_implementation(&read_value);

		graph.set_backbuffer_source("screen");

		graph.bake();
		graph.log();
	}

	void render_frame(double, double) override
	{
		auto &device = get_wsi().get_device();
		graph.setup_attachments(device, &device.get_swapchain_view());
		graph.enqueue_render_passes(device);
	}

private:
	RenderGraph graph;
	RPImpl clear_screen;
	ReadValueImpl read_value;
	WriteValueImpl write_value;

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
