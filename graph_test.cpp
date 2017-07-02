#include "render_graph.hpp"
#include "application.hpp"

using namespace Granite;

int Granite::application_main(int, char **)
{
	RenderGraph graph;
	AttachmentInfo info;

	ResourceDimensions dim;
	dim.width = 1280;
	dim.height = 720;
	dim.format = VK_FORMAT_B8G8R8A8_SRGB;
	graph.set_backbuffer_dimensions(dim);

	auto &pass0 = graph.add_pass("pass0");
	pass0.add_color_output("a", info);
	pass0.add_color_output("b", info);

	auto &pass1 = graph.add_pass("pass1");
	pass1.add_color_output("a1", info);
	pass1.add_color_input("a");

	auto &pass2 = graph.add_pass("pass2");
	pass2.add_color_output("screen", info);
	pass2.add_texture_input("a1");
	pass2.add_texture_input("b");

	graph.set_backbuffer_source("screen");

	graph.bake();
	graph.log();

	return 0;
}