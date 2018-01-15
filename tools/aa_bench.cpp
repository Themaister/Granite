#include "application.hpp"
#include "os.hpp"
#include "cli_parser.hpp"
#include "post/aa.hpp"
#include "post/temporal.hpp"
#include "post/hdr.hpp"

using namespace Util;
using namespace Granite;
using namespace Vulkan;

class AABenchApplication : public Application, public EventHandler
{
public:
	AABenchApplication(const std::string &input, const char *method);
	void render_frame(double, double) override;

private:
	std::string input_path;
	PostAAType type;
	void on_device_created(const DeviceCreatedEvent &e);
	void on_device_destroyed(const DeviceCreatedEvent &e);
	void on_swapchain_changed(const SwapchainParameterEvent &e);
	void on_swapchain_destroyed(const SwapchainParameterEvent &e);

	Texture *image = nullptr;
	RenderGraph graph;
	TemporalJitter jitter;
	bool need_main_pass = false;
};

AABenchApplication::AABenchApplication(const std::string &input, const char *method)
	: input_path(input)
{
	type = string_to_post_antialiasing_type(method);

	EVENT_MANAGER_REGISTER_LATCH(AABenchApplication, on_swapchain_changed, on_swapchain_destroyed, SwapchainParameterEvent);
	EVENT_MANAGER_REGISTER_LATCH(AABenchApplication, on_device_created, on_device_destroyed, DeviceCreatedEvent);
}

void AABenchApplication::render_frame(double, double)
{
	auto &wsi = get_wsi();
	auto &device = wsi.get_device();
	graph.setup_attachments(device, &device.get_swapchain_view());
	graph.enqueue_render_passes(device);
	need_main_pass = false;
}

void AABenchApplication::on_swapchain_changed(const SwapchainParameterEvent &swap)
{
	graph.reset();
	graph.set_device(&swap.get_device());

	ResourceDimensions dim;
	dim.width = swap.get_width();
	dim.height = swap.get_height();
	dim.format = swap.get_format();
	graph.set_backbuffer_dimensions(dim);

	AttachmentInfo main_output;
	main_output.format = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
	AttachmentInfo main_depth;
	main_depth.format = swap.get_device().get_default_depth_format();

	auto &pass = graph.add_pass("main", VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
	pass.add_color_output("tonemapped", main_output);
	pass.set_depth_stencil_output("depth-main", main_depth);
	pass.set_need_render_pass([this]() {
		return need_main_pass;
	});
	pass.set_build_render_pass([&](Vulkan::CommandBuffer &cmd) {
		Vulkan::CommandBufferUtil::set_quad_vertex_state(cmd);
		cmd.set_texture(0, 0, image->get_image()->get_view(), Vulkan::StockSampler::LinearClamp);
		Vulkan::CommandBufferUtil::draw_quad(cmd, "builtin://shaders/quad.vert", "builtin://shaders/blit.frag", {});
	});
	pass.set_get_clear_depth_stencil([](VkClearDepthStencilValue *value) -> bool {
		if (value)
		{
			value->depth = 0.0f;
			value->stencil = 0;
		}
		return true;
	});

	bool resolved = setup_before_post_chain_antialiasing(type,
														 graph, jitter,
														 "HDR-main", "depth-main", "HDR-resolved");

	if (setup_after_post_chain_antialiasing(type, graph, jitter,
	                                        resolved ? "HDR-resolved" : "HDR-main", "depth-main", "post-aa-output"))
		graph.set_backbuffer_source("post-aa-output");
	else
		graph.set_backbuffer_source("tonemapped");

	graph.bake();
	graph.log();
	need_main_pass = true;
}

void AABenchApplication::on_swapchain_destroyed(const SwapchainParameterEvent &)
{}

void AABenchApplication::on_device_created(const DeviceCreatedEvent &e)
{
	image = e.get_device().get_texture_manager().request_texture(input_path);
}

void AABenchApplication::on_device_destroyed(const DeviceCreatedEvent &e)
{
	image = nullptr;
}

namespace Granite
{
Application *application_create(int argc, char **argv)
{
	if (argc < 1)
		return nullptr;

	application_dummy();

	const char *aa_method = nullptr;
	std::string input_image;

#ifdef ANDROID
	input_image = "assets://image.png";
#endif

	CLICallbacks cbs;
	cbs.add("--aa-method", [&](CLIParser &parser) { aa_method = parser.next_string(); });
	cbs.add("--input-image", [&](CLIParser &parser) { input_image = parser.next_string(); });

	CLIParser parser(std::move(cbs), argc - 1, argv + 1);
	if (!parser.parse())
		return nullptr;

	if (input_image.empty())
	{
		LOGE("Need path to input image.\n");
		return nullptr;
	}

#ifdef ASSET_DIRECTORY
	const char *asset_dir = getenv("ASSET_DIRECTORY");
	if (!asset_dir)
		asset_dir = ASSET_DIRECTORY;

	Filesystem::get().register_protocol("assets", std::unique_ptr<FilesystemBackend>(new OSFilesystem(asset_dir)));
#endif

	try
	{
		auto *app = new AABenchApplication(input_image, aa_method);
		return app;
	}
	catch (const std::exception &e)
	{
		LOGE("application_create() threw exception: %s\n", e.what());
		return nullptr;
	}
}
}
