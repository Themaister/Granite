#include "vulkan.hpp"
#include "wsi.hpp"
#include "scene.hpp"
#include "render_context.hpp"
#include "camera.hpp"
#include "mesh_util.hpp"
#include "renderer.hpp"
#include "material_manager.hpp"
#include "gltf.hpp"
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

	Scene scene;
	VisibilityList visible;

	auto skybox = Util::make_abstract_handle<AbstractRenderable, Skybox>("assets://textures/skybox.ktx");
	scene.create_renderable(skybox);

	auto &device = wsi.get_device();

	GLTF::Parser parser("assets://scenes/TwoSidedPlane.gltf");
	for (auto &mesh : parser.get_meshes())
	{
		MaterialInfo default_material;
		default_material.uniform_base_color = vec4(0.0f, 1.0f, 0.0f, 1.0f);
		AbstractRenderableHandle gltf;
		if (mesh.has_material)
			gltf = Util::make_abstract_handle<AbstractRenderable, ImportedMesh>(mesh, parser.get_materials()[mesh.material_index]);
		else
			gltf = Util::make_abstract_handle<AbstractRenderable, ImportedMesh>(mesh, default_material);
		scene.create_renderable(gltf);
	}

	Renderer renderer;

	while (wsi.alive())
	{
		Filesystem::get().poll_notifications();
		wsi.begin_frame();

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