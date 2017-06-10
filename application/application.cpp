#include "application.hpp"
#include <stdexcept>
#include "sprite.hpp"
#include "horizontal_packing.hpp"

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
	: Application(width, height)
{
	scene_loader.load_scene(path);
	animation_system = scene_loader.consume_animation_system();

	cam.look_at(vec3(0.0f, 0.0f, 8.0f), vec3(0.0f));
	context.set_camera(cam);

	auto &ui = UI::UIManager::get();
	window = ui.add_child<UI::Window>();
	auto *w0 = window->add_child<UI::Widget>();
	auto *w1 = window->add_child<UI::Widget>();
	auto *w2 = window->add_child<UI::Widget>();
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

	scene.update_cached_transforms();
	scene.gather_visible_opaque_renderables(context.get_visibility_frustum(), visible);
	scene.gather_background_renderables(visible);

	auto cmd = device.request_command_buffer();
	auto rp = device.get_swapchain_render_pass(SwapchainRenderPass::DepthStencil);
	cmd->begin_render_pass(rp);
	renderer.begin();
	renderer.push_renderables(context, visible);
	renderer.flush(*cmd, context);
	UI::UIManager::get().render(*cmd);
	cmd->end_render_pass();
	device.submit(cmd);
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
