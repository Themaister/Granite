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

SceneViewerApplication::SceneViewerApplication(const std::string &path, unsigned width, unsigned height)
	: Application(width, height),
      horiz("assets://shaders/quad.vert", "assets://shaders/blur.frag"),
      vert("assets://shaders/quad.vert", "assets://shaders/blur.frag")
{
	horiz.set_defines({{ "METHOD", 0 }});
	vert.set_defines({{ "METHOD", 3 }});

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

bool SceneViewerApplication::get_clear_depth_stencil(VkClearDepthStencilValue *value)
{
	if (value)
	{
		value->depth = 1.0f;
		value->stencil = 0;
	}
	return true;
}

bool SceneViewerApplication::get_clear_color(unsigned index, VkClearColorValue *value)
{
	if (value)
	{
		value->float32[0] = context.get_fog_parameters().color.r;
		value->float32[1] = context.get_fog_parameters().color.g;
		value->float32[2] = context.get_fog_parameters().color.b;
		value->float32[3] = 0.0f;
	}
	return true;
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
	AttachmentInfo backbuffer_depth = backbuffer;
	backbuffer_depth.format = swap.get_device().get_default_depth_stencil_format();

	AttachmentInfo info;
	info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	info.size_x = 1.0f;
	info.size_y = 1.0f;
	info.size_class = SizeClass::SwapchainRelative;

	auto &pass = graph.add_pass("main");
	pass.add_color_output("main", info);
	pass.set_depth_stencil_output("backbuffer_depth", backbuffer_depth);
	pass.set_implementation(this);

	auto &horiz = graph.add_pass("blur_horiz");
	horiz.add_color_output("horiz", info);
	horiz.add_texture_input("main");
	horiz.set_implementation(&this->horiz);

	auto &vert = graph.add_pass("blur_vert");
	vert.add_color_output("backbuffer", backbuffer);
	vert.add_texture_input("horiz");
	vert.set_implementation(&this->vert);

	graph.bake();
	graph.log();
}

void SceneViewerApplication::build_render_pass(RenderPass &, Vulkan::CommandBuffer &cmd)
{
	renderer.flush(cmd, context);
	UI::UIManager::get().render(cmd);
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
