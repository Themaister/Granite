#include "vulkan.hpp"
#include "wsi.hpp"
#include "scene_loader.hpp"
#include "render_context.hpp"
#include "camera.hpp"
#include "mesh_util.hpp"
#include "renderer.hpp"
#include "material_manager.hpp"
#include "gltf.hpp"
#include "animation_system.hpp"
#include "importers.hpp"
#include <string.h>

using namespace Vulkan;
using namespace Granite;
using namespace Util;

int main()
{
	Vulkan::WSI wsi;
	wsi.init(1280, 720);

	RenderContext context;
	FPSCamera cam;
	cam.look_at(vec3(0.0f, 0.0f, 8.0f), vec3(0.0f));
	context.set_camera(cam);

	VisibilityList visible;

	SceneLoader loader;
	loader.load_scene("assets://scenes/test.json");
	auto &scene = loader.get_scene();
	auto animation = loader.consume_animation_system();

	auto &device = wsi.get_device();

	Renderer renderer;
	while (wsi.alive())
	{
		Filesystem::get().poll_notifications();
		wsi.begin_frame();

		animation->animate(wsi.get_elapsed_time());
		context.set_camera(cam);
		visible.clear();

		scene.update_cached_transforms();
		scene.gather_visible_opaque_renderables(context.get_visibility_frustum(), visible);
		scene.gather_background_renderables(visible);

		auto cmd = device.request_command_buffer();
		auto rp = device.get_swapchain_render_pass(SwapchainRenderPass::DepthStencil);
		rp.clear_color->float32[0] = 0.2f;
		rp.clear_color->float32[1] = 0.2f;
		rp.clear_color->float32[2] = 0.3f;
		cmd->begin_render_pass(rp);
		renderer.render(*cmd, context, visible);
		cmd->end_render_pass();
		device.submit(cmd);

		wsi.end_frame();
	}
}