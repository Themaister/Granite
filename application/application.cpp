#include "application.hpp"
#include <stdexcept>
#include "sprite.hpp"
#include "horizontal_packing.hpp"
#include "image_widget.hpp"
#include "label.hpp"

using namespace std;
using namespace Vulkan;

namespace Granite
{
Application::Application(unsigned width, unsigned height)
{
	EventManager::get_global();
	Filesystem::get();

	platform = create_default_application_platform(width, height);

	if (!wsi.init(platform.get(), width, height))
		throw runtime_error("Failed to initialize WSI.");
}

bool SceneViewerApplication::GBufferImpl::get_clear_color(unsigned index, VkClearColorValue *value)
{
	if (value)
		memset(value, 0, sizeof(*value));
	return true;
}

bool SceneViewerApplication::GBufferImpl::get_clear_depth_stencil(VkClearDepthStencilValue *value)
{
	if (value)
	{
		value->stencil = 0;
		value->depth = 1.0f;
	}

	return true;
}

void SceneViewerApplication::GBufferImpl::build_render_pass(RenderPass &pass, Vulkan::CommandBuffer &cmd)
{
	app->renderer.flush(cmd, app->context);
}

void SceneViewerApplication::LightingImpl::build_render_pass(RenderPass &, Vulkan::CommandBuffer &)
{

}

void SceneViewerApplication::UIImpl::build_render_pass(RenderPass &, Vulkan::CommandBuffer &cmd)
{
	UI::UIManager::get().render(cmd);
}

SceneViewerApplication::SceneViewerApplication(const std::string &path, unsigned width, unsigned height)
	: Application(width, height),
	  gbuffer_impl(this),
	  lighting_impl(this),
	  ui_impl(this)
{
	scene_loader.load_scene(path);
	animation_system = scene_loader.consume_animation_system();

	auto *environment = scene_loader.get_scene().get_environment();
	if (environment)
		context.set_fog_parameters(environment->fog);

	cam.look_at(vec3(0.0f, 0.0f, 8.0f), vec3(0.0f));
	context.set_camera(cam);

	auto &ui = UI::UIManager::get();
	window = ui.add_child<UI::Window>();
	auto *w0 = window->add_child<UI::Widget>();
	auto *w1 = window->add_child<UI::Widget>();
	auto *w2 = window->add_child<UI::Widget>();
	auto *image = window->add_child<UI::Image>("assets://gltf-sandbox/textures/maister.png");
	image->set_minimum_geometry(image->get_target_geometry() * vec2(1.0f / 16.0f));
	image->set_keep_aspect_ratio(true);
	auto *w3 = window->add_child<UI::Widget>();
	w0->set_background_color(vec4(1.0f, 0.0f, 0.0f, 1.0f));
	w1->set_background_color(vec4(0.0f, 1.0f, 0.0f, 1.0f));
	w2->set_background_color(vec4(1.0f, 1.0f, 0.0f, 1.0f));
	w3->set_background_color(vec4(0.0f, 1.0f, 1.0f, 1.0f));
	w0->set_target_geometry(vec2(400.0f, 60.0f));
	w1->set_target_geometry(vec2(400.0f, 60.0f));
	w2->set_target_geometry(vec2(400.0f, 60.0f));
	w3->set_target_geometry(vec2(40.0f, 60.0f));
	w0->set_minimum_geometry(vec2(40.0f, 10.0f));
	w1->set_minimum_geometry(vec2(40.0f, 10.0f));
	w2->set_minimum_geometry(vec2(40.0f, 10.0f));
	w3->set_minimum_geometry(vec2(40.0f, 10.0f));
	window->set_target_geometry(ivec2(10));

	auto *label = window->add_child<UI::Label>("Hai :D");
	label->set_margin(20.0f);
	label->set_color(vec4(0.0f, 0.0f, 0.0f, 1.0f));
	label->set_font_alignment(Font::Alignment::Center);

	auto *w4 = window->add_child<UI::HorizontalPacking>();
	w4->set_margin(10.0f);
	auto *tmp = w4->add_child<UI::Widget>();
	tmp->set_background_color(vec4(0.0f, 0.0f, 0.0f, 1.0f));
	tmp->set_minimum_geometry(vec2(50.0f));
	//tmp->set_target_geometry(vec2(50.0f));
	tmp = w4->add_child<UI::Widget>();
	tmp->set_background_color(vec4(0.0f, 0.0f, 0.0f, 1.0f));
	tmp->set_minimum_geometry(vec2(50.0f));
	//tmp->set_target_geometry(vec2(50.0f));

	w2->set_size_is_flexible(true);

	EventManager::get_global().register_latch_handler(SwapchainParameterEvent::type_id,
	                                                  &SceneViewerApplication::on_swapchain_changed,
	                                                  &SceneViewerApplication::on_swapchain_destroyed,
	                                                  this);
}

void SceneViewerApplication::on_swapchain_changed(const Event &e)
{
	auto &swap = e.as<SwapchainParameterEvent>();
	graph.reset();

	ResourceDimensions dim;
	dim.width = swap.get_width();
	dim.height = swap.get_height();
	dim.format = swap.get_format();
	graph.set_backbuffer_dimensions(dim);
	graph.set_backbuffer_source("backbuffer");

	AttachmentInfo backbuffer;
	AttachmentInfo emissive, albedo, normal, pbr, depth;
	emissive.format = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
	albedo.format = VK_FORMAT_R8G8B8A8_SRGB;
	normal.format = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
	pbr.format = VK_FORMAT_R8G8_UNORM;
	depth.format = swap.get_device().get_default_depth_stencil_format();

	auto &gbuffer = graph.add_pass("gbuffer", VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
	gbuffer.add_color_output("emissive", emissive);
	gbuffer.add_color_output("albedo", albedo);
	gbuffer.add_color_output("normal", normal);
	gbuffer.add_color_output("pbr", pbr);
	gbuffer.set_depth_stencil_output("depth", depth);
	gbuffer.set_implementation(&gbuffer_impl);

	auto &lighting = graph.add_pass("lighting", VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
	lighting.add_color_output("HDR", emissive, "emissive");
	lighting.add_attachment_input("albedo");
	lighting.add_attachment_input("normal");
	lighting.add_attachment_input("pbr");
	lighting.set_depth_stencil_input("depth");
	lighting.set_implementation(&lighting_impl);

	auto &ui = graph.add_pass("ui", VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
	ui.add_color_output("backbuffer", backbuffer, "HDR");
	ui.set_implementation(&ui_impl);

	graph.bake();
	graph.log();
}

void SceneViewerApplication::on_swapchain_destroyed(const Event &)
{
}

void SceneViewerApplication::render_frame(double, double elapsed_time)
{
	auto &wsi = get_wsi();
	auto &scene = scene_loader.get_scene();
	auto &device = wsi.get_device();

	animation_system->animate(elapsed_time);
	context.set_camera(cam);
	visible.clear();

	window->set_background_color(vec4(1.0f));
	window->set_margin(5);
	window->set_floating_position(ivec2(40));
	window->set_title("My Window");
	//window->set_target_geometry(window->get_target_geometry() + vec2(1.0f));

	scene.update_cached_transforms();
	scene.refresh_per_frame(context);
	scene.gather_visible_opaque_renderables(context.get_visibility_frustum(), visible);
	scene.gather_background_renderables(visible);

	renderer.begin();
	renderer.push_renderables(context, visible);
	graph.setup_attachments(device, &device.get_swapchain_view());
	graph.enqueue_render_passes(device);
}

int Application::run()
{
	auto &wsi = get_wsi();
	while (get_platform().alive(wsi))
	{
		Filesystem::get().poll_notifications();
		wsi.begin_frame();
		render_frame(wsi.get_platform().get_frame_timer().get_frame_time(),
					 wsi.get_platform().get_frame_timer().get_elapsed());
		wsi.end_frame();
	}
	return 0;
}

}
