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
}

void SceneViewerApplication::render_frame(double, double elapsed_time)
{
	auto &wsi = get_wsi();
	auto &scene = scene_loader.get_scene();
	auto &device = wsi.get_device();

	SpriteList sprites;
	auto sprite = Util::make_abstract_handle<AbstractRenderable, Sprite>();
	auto *s = static_cast<Sprite *>(sprite.get());
	s->size = ivec2(16, 16);
	s->color = vec4(1.0f, 0.0f, 0.0f, 1.0f);
	s->pipeline = MeshDrawPipeline::Opaque;

	auto sprite2 = Util::make_abstract_handle<AbstractRenderable, Sprite>();
	auto *s2 = static_cast<Sprite *>(sprite2.get());
	s2->size = ivec2(16, 16);
	s2->color = vec4(0.0f, 1.0f, 0.0f, 1.0f);
	s2->pipeline = MeshDrawPipeline::AlphaBlend;

	auto sprite3 = Util::make_abstract_handle<AbstractRenderable, Sprite>();
	auto *s3 = static_cast<Sprite *>(sprite3.get());
	s3->size = ivec2(16, 16);
	s3->color = vec4(0.0f, 0.0f, 1.0f, 1.0f);
	s3->pipeline = MeshDrawPipeline::AlphaBlend;

	sprites.push_back({ sprite, vec3(16.0f, 16.0f, 0.1f) });
	sprites.push_back({ sprite, vec3(64.0f, 16.0f, 0.1f) });
	sprites.push_back({ sprite, vec3(64.0f, 64.0f, 0.1f) });
	sprites.push_back({ sprite, vec3(0.0f, 64.0f, 0.1f) });
	sprites.push_back({ sprite2, vec3(100.0f, 16.0f, 0.2f) });
	sprites.push_back({ sprite3, vec3(100.0f, 16.0f, 0.19f) });

	animation_system->animate(elapsed_time);
	context.set_camera(cam);
	visible.clear();

	scene.update_cached_transforms();
	scene.gather_visible_opaque_renderables(context.get_visibility_frustum(), visible);
	scene.gather_background_renderables(visible);

	auto cmd = device.request_command_buffer();
	auto rp = device.get_swapchain_render_pass(SwapchainRenderPass::DepthStencil);
	cmd->begin_render_pass(rp);
	renderer.render(*cmd, context, visible);
	renderer.render_sprites(*cmd,
	                        vec2(0.0f),
	                        vec2(cmd->get_viewport().width, cmd->get_viewport().height),
	                        sprites);
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
