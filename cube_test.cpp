#include "vulkan.hpp"
#include "wsi.hpp"
#include "scene.hpp"
#include "render_context.hpp"
#include "camera.hpp"
#include "mesh_util.hpp"
#include "renderer.hpp"

using namespace Vulkan;
using namespace Granite;

int main()
{
	Vulkan::WSI wsi;
	wsi.init(1280, 720);

	RenderContext context;
	Camera cam;
	cam.look_at(vec3(0.0f, 0.0f, 10.0f), vec3(0.0f));
	context.set_camera(cam);

	Scene scene;
	scene.create_renderable(Util::make_abstract_handle<AbstractRenderable, CubeMesh>());
	VisibilityList visible;

	Renderer renderer;

	auto &device = wsi.get_device();

	while (wsi.alive())
	{
		wsi.begin_frame();

		visible.clear();
		scene.update_cached_transforms();
		scene.gather_visible_opaque_renderables(context.get_visibility_frustum(), visible);

		auto cmd = device.request_command_buffer();
		auto rp = device.get_swapchain_render_pass(SwapchainRenderPass::DepthStencil);
		rp.clear_color->float32[0] = 0.2f;
		rp.clear_color->float32[1] = 0.2f;
		rp.clear_color->float32[2] = 0.2f;
		cmd->begin_render_pass(rp);
		renderer.render(*cmd, context, visible);
		cmd->end_render_pass();
		device.submit(cmd);

		wsi.end_frame();
	}
}