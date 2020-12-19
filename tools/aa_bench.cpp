#include "application.hpp"
#include "os_filesystem.hpp"
#include "cli_parser.hpp"
#include "post/aa.hpp"
#include "post/temporal.hpp"
#include "post/hdr.hpp"
#include "task_composer.hpp"

using namespace Util;
using namespace Granite;
using namespace Vulkan;

class AABenchApplication : public Application, public EventHandler
{
public:
	AABenchApplication(const std::string &input0, const std::string &input1, const char *method);
	void render_frame(double, double) override;

private:
	std::string input_path0;
	std::string input_path1;
	PostAAType type;
	void on_device_created(const DeviceCreatedEvent &e);
	void on_device_destroyed(const DeviceCreatedEvent &e);
	void on_swapchain_changed(const SwapchainParameterEvent &e);
	void on_swapchain_destroyed(const SwapchainParameterEvent &e);

	Texture *images[2] = {};
	RenderGraph graph;
	TemporalJitter jitter;
	bool need_main_pass = false;
	unsigned input_index = 0;
};

AABenchApplication::AABenchApplication(const std::string &input0, const std::string &input1, const char *method)
	: input_path0(input0), input_path1(input1)
{
	type = string_to_post_antialiasing_type(method);

	EVENT_MANAGER_REGISTER_LATCH(AABenchApplication, on_swapchain_changed, on_swapchain_destroyed, SwapchainParameterEvent);
	EVENT_MANAGER_REGISTER_LATCH(AABenchApplication, on_device_created, on_device_destroyed, DeviceCreatedEvent);
}

void AABenchApplication::render_frame(double, double)
{
	jitter.step(mat4(1.0f), mat4(1.0f));

	auto &wsi = get_wsi();
	auto &device = wsi.get_device();
	graph.setup_attachments(device, &device.get_swapchain_view());
	TaskComposer composer(*Global::thread_group());
	graph.enqueue_render_passes(device, composer);
	composer.get_outgoing_task()->wait();
	//need_main_pass = false;
}

void AABenchApplication::on_swapchain_changed(const SwapchainParameterEvent &swap)
{
	graph.reset();
	ImplementationQuirks::get().use_async_compute_post = false;
	ImplementationQuirks::get().render_graph_force_single_queue = true;

	ResourceDimensions dim;
	dim.width = swap.get_width();
	dim.height = swap.get_height();
	dim.format = swap.get_format();
	dim.transform = swap.get_prerotate();
	graph.set_backbuffer_dimensions(dim);

	AttachmentInfo main_output;
	main_output.format = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
	AttachmentInfo main_depth;
	main_depth.format = swap.get_device().get_default_depth_format();

	AttachmentInfo swapchain_output;

	auto &pass = graph.add_pass("main", RENDER_GRAPH_QUEUE_GRAPHICS_BIT);
	pass.add_color_output("HDR-main", main_output);
	pass.set_depth_stencil_output("depth-main", main_depth);
	pass.set_build_render_pass([&](Vulkan::CommandBuffer &cmd) {
		cmd.set_texture(0, 0, images[(input_index++) & 1]->get_image()->get_view(), Vulkan::StockSampler::LinearClamp);
		Vulkan::CommandBufferUtil::draw_fullscreen_quad(cmd, "builtin://shaders/quad.vert",
		                                                "builtin://shaders/blit.frag", {});
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

	auto &tonemap = graph.add_pass("tonemap", RENDER_GRAPH_QUEUE_GRAPHICS_BIT);
	tonemap.add_color_output("tonemap", swapchain_output);
	auto &tonemap_res = tonemap.add_texture_input(resolved ? "HDR-resolved" : "HDR-main");
	tonemap.set_build_render_pass([&](Vulkan::CommandBuffer &cmd) {
		auto &input = graph.get_physical_texture_resource(tonemap_res);
		cmd.set_texture(0, 0, input, Vulkan::StockSampler::NearestClamp);

		Vulkan::CommandBufferUtil::setup_fullscreen_quad(cmd, "builtin://shaders/quad.vert",
		                                                 "builtin://shaders/blit.frag", {});
		cmd.set_specialization_constant_mask(1);
		cmd.set_specialization_constant(0, float((input_index % 3) + 1) / 3.0f);
		Vulkan::CommandBufferUtil::draw_fullscreen_quad(cmd);
	});

	if (setup_after_post_chain_antialiasing(type, graph, jitter,
	                                        "tonemap", "depth-main", "post-aa-output"))
		graph.set_backbuffer_source("post-aa-output");
	else
		graph.set_backbuffer_source("tonemap");

	graph.bake();
	graph.log();
	need_main_pass = true;
}

void AABenchApplication::on_swapchain_destroyed(const SwapchainParameterEvent &)
{}

void AABenchApplication::on_device_created(const DeviceCreatedEvent &e)
{
	images[0] = e.get_device().get_texture_manager().request_texture(input_path0);
	images[1] = e.get_device().get_texture_manager().request_texture(input_path1);
	graph.set_device(&e.get_device());
}

void AABenchApplication::on_device_destroyed(const DeviceCreatedEvent &)
{
	memset(images, 0, sizeof(images));
	graph.reset();
	graph.set_device(nullptr);
}

namespace Granite
{
Application *application_create(int argc, char **argv)
{
	if (argc < 1)
		return nullptr;

	application_dummy();

	const char *aa_method = nullptr;
	std::string input_image0;
	std::string input_image1;

#ifdef ANDROID
	input_image0 = "assets://image0.png";
	input_image1 = "assets://image1.png";
#endif

	CLICallbacks cbs;
	cbs.add("--aa-method", [&](CLIParser &parser) { aa_method = parser.next_string(); });
	cbs.add("--input-images", [&](CLIParser &parser) { input_image0 = parser.next_string(); input_image1 = parser.next_string(); });

	CLIParser parser(std::move(cbs), argc - 1, argv + 1);
	if (!parser.parse())
		return nullptr;

	if (input_image0.empty() || input_image1.empty())
	{
		LOGE("Need path to input images.\n");
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
		auto *app = new AABenchApplication(input_image0, input_image1, aa_method);
		return app;
	}
	catch (const std::exception &e)
	{
		LOGE("application_create() threw exception: %s\n", e.what());
		return nullptr;
	}
}
}
