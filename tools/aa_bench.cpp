#include "application.hpp"
#include "os_filesystem.hpp"
#include "cli_parser.hpp"
#include "post/aa.hpp"
#include "post/temporal.hpp"
#include "post/hdr.hpp"
#include "task_composer.hpp"
#include "render_context.hpp"

using namespace Util;
using namespace Granite;
using namespace Vulkan;

class AABenchApplication : public Application, public EventHandler
{
public:
	AABenchApplication(const std::string &input0, const std::string &input1, const char *method, float scale);
	void render_frame(double, double) override;

private:
	std::string input_path0;
	std::string input_path1;
	float scale;
	PostAAType type;
	void on_device_created(const DeviceCreatedEvent &e);
	void on_device_destroyed(const DeviceCreatedEvent &e);
	void on_swapchain_changed(const SwapchainParameterEvent &e);
	void on_swapchain_destroyed(const SwapchainParameterEvent &e);

	AssetID images[2] = {};
	RenderGraph graph;
	TemporalJitter jitter;
	RenderContext render_context;
	bool need_main_pass = false;
	unsigned input_index = 0;
};

AABenchApplication::AABenchApplication(const std::string &input0, const std::string &input1, const char *method, float scale_)
	: input_path0(input0), input_path1(input1), scale(scale_)
{
	type = string_to_post_antialiasing_type(method);
	images[0] = input_path0.empty() ? AssetID{} : GRANITE_ASSET_MANAGER()->register_asset(*GRANITE_FILESYSTEM(),
	                                                                                      input_path0,
	                                                                                      AssetClass::ImageColor);
	images[1] = input_path1.empty() ? AssetID{} : GRANITE_ASSET_MANAGER()->register_asset(*GRANITE_FILESYSTEM(),
	                                                                                      input_path1,
	                                                                                      AssetClass::ImageColor);
	EVENT_MANAGER_REGISTER_LATCH(AABenchApplication, on_swapchain_changed, on_swapchain_destroyed, SwapchainParameterEvent);
	EVENT_MANAGER_REGISTER_LATCH(AABenchApplication, on_device_created, on_device_destroyed, DeviceCreatedEvent);
}

void AABenchApplication::render_frame(double, double)
{
	jitter.step(mat4(1.0f), mat4(1.0f));

	auto &wsi = get_wsi();
	auto &device = wsi.get_device();
	graph.setup_attachments(device, &device.get_swapchain_view());
	TaskComposer composer(*GRANITE_THREAD_GROUP());
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

	main_output.size_x = scale;
	main_output.size_y = scale;
	main_depth.size_x = scale;
	main_depth.size_y = scale;

	AttachmentInfo swapchain_output;

	auto &pass = graph.add_pass("main", RENDER_GRAPH_QUEUE_GRAPHICS_BIT);
	pass.add_color_output("HDR-main", main_output);
	pass.set_depth_stencil_output("depth-main", main_depth);
	pass.set_get_clear_color([](unsigned, VkClearColorValue *value) -> bool {
		if (value)
			memset(value, 0, sizeof(*value));
		return true;
	});
	pass.set_build_render_pass([&](Vulkan::CommandBuffer &cmd) {
		auto img = images[(input_index++) & 1];
		if (img)
		{
			cmd.set_texture(0, 0, *cmd.get_device().get_resource_manager().get_image_view_blocking(img),
			                Vulkan::StockSampler::LinearClamp);
			Vulkan::CommandBufferUtil::draw_fullscreen_quad(cmd, "builtin://shaders/quad.vert",
			                                                "builtin://shaders/blit.frag", {});
		}
	});
	pass.set_get_clear_depth_stencil([](VkClearDepthStencilValue *value) -> bool {
		if (value)
		{
			value->depth = 0.0f;
			value->stencil = 0;
		}
		return true;
	});

	Camera cam;
	cam.look_at(vec3(0.0f, 0.0f, +1.0f), vec3(0.0f), vec3(0.0f, 1.0f, 0.0f));
	cam.set_depth_range(1.0f, 1000.0f);
	cam.set_fovy(0.5f);
	render_context.set_camera(cam);

	bool resolved = setup_before_post_chain_antialiasing(type,
	                                                     graph, jitter,
	                                                     render_context,
	                                                     scale,
	                                                     "HDR-main", "depth-main", "", "HDR-resolved");

	auto &tonemap = graph.add_pass("tonemap", RENDER_GRAPH_QUEUE_GRAPHICS_BIT);
	tonemap.add_color_output("tonemap", swapchain_output);
	auto &tonemap_res = tonemap.add_texture_input(resolved ? "HDR-resolved" : "HDR-main");
	tonemap.set_build_render_pass([&](Vulkan::CommandBuffer &cmd) {
		auto &input = graph.get_physical_texture_resource(tonemap_res);
		cmd.set_texture(0, 0, input, Vulkan::StockSampler::NearestClamp);

		Vulkan::CommandBufferUtil::setup_fullscreen_quad(cmd, "builtin://shaders/quad.vert",
		                                                 "builtin://shaders/blit.frag", {});
		Vulkan::CommandBufferUtil::draw_fullscreen_quad(cmd);
	});

	const char *backbuffer_source;

	if (setup_after_post_chain_antialiasing(type, graph, jitter, scale,
	                                        "tonemap", "depth-main", "post-aa-output"))
		backbuffer_source = "post-aa-output";
	else
		backbuffer_source = "tonemap";

	if (scale < 1.0f)
	{
		setup_after_post_chain_upscaling(graph, backbuffer_source, "fidelityfx-fsr", true);
		backbuffer_source = "fidelityfx-fsr";
	}

	graph.set_backbuffer_source(backbuffer_source);
	graph.enable_timestamps(true);

	graph.bake();
	graph.log();
	need_main_pass = true;
}

void AABenchApplication::on_swapchain_destroyed(const SwapchainParameterEvent &)
{
}

void AABenchApplication::on_device_created(const DeviceCreatedEvent &e)
{
	graph.set_device(&e.get_device());
}

void AABenchApplication::on_device_destroyed(const DeviceCreatedEvent &)
{
	graph.reset();
	graph.set_device(nullptr);
}

namespace Granite
{
Application *application_create(int argc, char **argv)
{
	if (argc < 1)
		return nullptr;

	GRANITE_APPLICATION_SETUP_FILESYSTEM();

	const char *aa_method = nullptr;
	std::string input_image0;
	std::string input_image1;
	float scale = 1.0f;

#ifdef ANDROID
	input_image0 = "assets://image0.png";
	input_image1 = "assets://image1.png";
#endif

	CLICallbacks cbs;
	cbs.add("--aa-method", [&](CLIParser &parser) { aa_method = parser.next_string(); });
	cbs.add("--input-images", [&](CLIParser &parser) { input_image0 = parser.next_string(); input_image1 = parser.next_string(); });
	cbs.add("--scale", [&](CLIParser &parser) { scale = float(parser.next_double()); });

	CLIParser parser(std::move(cbs), argc - 1, argv + 1);
	if (!parser.parse())
		return nullptr;

	try
	{
		auto *app = new AABenchApplication(input_image0, input_image1, aa_method, scale);
		return app;
	}
	catch (const std::exception &e)
	{
		LOGE("application_create() threw exception: %s\n", e.what());
		return nullptr;
	}
}
}
