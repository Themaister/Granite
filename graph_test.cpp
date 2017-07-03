#include "render_graph.hpp"
#include "application.hpp"

using namespace Granite;

class RPImpl : public RenderPassImplementation
{
public:
	void build_render_pass(RenderGraph &, Vulkan::CommandBuffer &) override
	{

	}
};

int Granite::application_main(int, char **)
{
	RenderGraph graph;
	AttachmentInfo info;
	info.size_x = 2.0f;
	info.size_y = 2.0f;

	RPImpl impl;

	ResourceDimensions dim;
	dim.width = 1280;
	dim.height = 720;
	dim.format = VK_FORMAT_B8G8R8A8_SRGB;
	graph.set_backbuffer_dimensions(dim);

	auto &pass0 = graph.add_pass("pass0");
	pass0.add_color_output("a", info);
	pass0.add_color_output("b", info);
	pass0.set_depth_stencil_output("c", info);
	pass0.set_implementation(&impl);

	auto &pass1 = graph.add_pass("pass1");
	pass1.add_color_output("a1", info);
	pass1.add_color_input("a");
	pass1.add_texture_input("b");
	pass1.set_depth_stencil_output("c1", info);
	pass1.set_implementation(&impl);

	auto &pass2 = graph.add_pass("pass2");
	pass2.add_color_output("screen1", info);
	pass2.add_attachment_input("a1");
	pass2.set_depth_stencil_input("c1");
	pass2.add_texture_input("b");
	pass2.add_attachment_input("c1");
	pass2.set_implementation(&impl);

	auto &pass3 = graph.add_pass("pass3");
	pass3.add_color_output("screen", info);
	pass3.add_texture_input("c1");
	pass3.add_color_input("screen1");
	pass3.set_implementation(&impl);

	graph.set_backbuffer_source("screen");

	graph.bake();
	graph.log();

	return 0;
}