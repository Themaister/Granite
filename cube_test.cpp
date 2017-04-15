#include "vulkan.hpp"
#include "wsi.hpp"
#include "scene.hpp"
#include "render_context.hpp"
#include "camera.hpp"
#include "mesh_util.hpp"
#include "renderer.hpp"
#include "material_manager.hpp"

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

	Scene scene;
	auto cube = Util::make_abstract_handle<AbstractRenderable, CubeMesh>();
	auto skybox = Util::make_abstract_handle<AbstractRenderable, Skybox>("assets://textures/skybox.ktx");
	auto entity = scene.create_renderable(cube);
	scene.create_renderable(skybox);
	auto *transform = entity->get_component<SpatialTransformComponent>();

	entity = scene.create_renderable(cube);
	entity->get_component<SpatialTransformComponent>()->translation = vec3(6.0f, 3.0f, 0.0f);
	entity->get_component<SpatialTransformComponent>()->scale = vec3(2.0f, 1.0f, 1.0f);
	VisibilityList visible;

	Renderer renderer;

	auto &device = wsi.get_device();

	while (wsi.alive())
	{
		Filesystem::get().poll_notifications();
		wsi.begin_frame();

		context.set_camera(cam);
		visible.clear();
		transform->rotation = normalize(rotate(transform->rotation, 0.01f, normalize(vec3(0.0f, 1.0f, 0.0f))));

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