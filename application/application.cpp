#include "application.hpp"
#include <stdexcept>
#include "sprite.hpp"

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
	font.reset(new Font("assets://font.ttf", 12));
}

void SceneViewerApplication::render_frame(double, double elapsed_time)
{
	auto &wsi = get_wsi();
	auto &scene = scene_loader.get_scene();
	auto &device = wsi.get_device();

	animation_system->animate(elapsed_time);
	context.set_camera(cam);
	visible.clear();

	flat_renderer.begin();

	Camera camera;
	camera.look_at(vec3(0.0f), vec3(1.0f, 0.0f, 0.0f));
	camera.set_depth_range(0.1f, 3.0f);
	mat4 inv_vp = inverse(camera.get_projection() * camera.get_view());
	Frustum f;
	f.build_planes(inv_vp);

	flat_renderer.reset_scissor();
	flat_renderer.push_scissor(vec2(10.0f), vec2(150.0f));

#if 1
	auto &view = device.get_texture_manager().request_texture("assets://textures/maister.png")->get_image()->get_view();
	flat_renderer.render_textured_quad(view, vec3(200.0f, 200.0f, 0.0f), vec2(64.0f), vec2(0.0f), vec2(400.0f));
	flat_renderer.render_quad(vec3(10.0f, 10.0f, 0.01f), vec2(256.0f), vec4(1.0f));
	flat_renderer.render_quad(vec3(30.0f, 30.0f, 0.015f), vec2(400.0f), vec4(0.8f, 0.0f, 0.0f, 0.4f));
#endif

	flat_renderer.pop_scissor();

	static const vec2 offsets[] = {
		{ 1.0f, 1.0f },
		{ 100.0f, 4.0f },
		{ 100.0f, 50.0f },
		{ 140.0f, 80.0f },
	};
	static const vec2 offsets2[] = {
		{ 40.0f, 11.0f },
		{ 60.0f, 11.0f },
		{ 80.0f, 12.0f },
		{ 10.0f, 18.0f },
	};
	flat_renderer.render_line_strip(offsets, 0.0f, 4, vec4(1.0f));
	flat_renderer.render_line_strip(offsets2, 0.0f, 4, vec4(1.0f, 0.0f, 0.0f, 1.0f));

	scene.update_cached_transforms();
	scene.gather_visible_opaque_renderables(context.get_visibility_frustum(), visible);
	scene.gather_background_renderables(visible);

	auto cmd = device.request_command_buffer();
	auto rp = device.get_swapchain_render_pass(SwapchainRenderPass::DepthStencil);
	cmd->begin_render_pass(rp);
	renderer.begin();
	renderer.push_renderables(context, visible);
	renderer.render_debug_frustum(context, f, vec4(0.0f, 0.0f, 1.0f, 1.0f));
	renderer.flush(*cmd, context);
	flat_renderer.flush(*cmd, vec3(0.0f), vec3(cmd->get_viewport().width, cmd->get_viewport().height, 1.0f));
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
