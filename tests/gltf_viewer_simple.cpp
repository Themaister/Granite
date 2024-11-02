/* Copyright (c) 2017-2024 Hans-Kristian Arntzen
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "application.hpp"
#include "device.hpp"
#include "renderer.hpp"
#include "flat_renderer.hpp"
#include "scene.hpp"
#include "scene_loader.hpp"
#include "render_context.hpp"
#include "ui_manager.hpp"

using namespace Granite;
using namespace Vulkan;
using namespace Util;

struct ViewerApplication : Application
{
	explicit ViewerApplication(const std::string &path)
		: renderer(RendererType::GeneralForward, /* resolver */ nullptr)
	{
		// Using Renderer directly is somewhat low level.
		// Normally, you would use RendererSuite and RenderPassSceneRenderer.

		// Effectively, loads a scene and inserts Entity objects into the Scene.
		scene_loader.load_scene(path);

		// Set initial position.
		fps_camera.set_position(vec3(0.0f, 0.0f, 5.0f));
		fps_camera.set_depth_range(0.1f, 500.0f);
	}

	void render_frame(double frame_time, double elapsed_time) override
	{
		// Simple serial variant.
		auto &scene = scene_loader.get_scene();

		// First, update game objects. Can modify their scene Node transforms.
		// Objects can be added as well.
		// Animation system could run here.
		scene_loader.get_animation_system().animate(frame_time, elapsed_time);

		// - Traverse the node hierarchy and compute full transform.
		// - Updates the model and skinning matrices.
		scene.update_all_transforms();

		// Update the rendering context.
		// Only use a single directional light.
		// No shadows or anything fancy is used.
		lighting.directional.color = vec3(1.0f, 0.9f, 0.8f);
		lighting.directional.direction = normalize(vec3(1.0f, 1.0f, 1.0f));
		context.set_lighting_parameters(&lighting);

		// The renderer can be configured to handle many different scenarios.
		// Here we reconfigure the renderer to work with the current lighting configuration.
		// This is particularly necessary for forward renderers.
		// For G-buffer renderers, only a few flags are relevant. This is handled automatically
		// by the more advanced APIs such as RendererSuite and the RenderPassSceneRenderer.
		renderer.set_mesh_renderer_options_from_lighting(lighting);

		// The FPS camera registers for input events.
		// Update all rendering matrices based on current Camera state.
		// It is possible to pass down explicit matrices as well.
		context.set_camera(fps_camera);

		// Simple forward renderer, so we render opaque, transparent and background renderables in one go.
		visible.clear();
		scene.gather_visible_opaque_renderables(context.get_visibility_frustum(), visible);
		scene.gather_visible_transparent_renderables(context.get_visibility_frustum(), visible);
		scene.gather_unbounded_renderables(visible);

		// Time to render.
		renderer.begin(queue);
		queue.push_renderables(context, visible.data(), visible.size());

		auto &device = get_wsi().get_device();
		auto cmd = device.request_command_buffer();
		auto rp = device.get_swapchain_render_pass(SwapchainRenderPass::Depth);
		rp.clear_color[0].float32[0] = 0.01f;
		rp.clear_color[0].float32[1] = 0.02f;
		rp.clear_color[0].float32[2] = 0.03f;
		cmd->begin_render_pass(rp);
		renderer.flush(*cmd, queue, context, 0, nullptr);

		// Render some basic 2D on top.
		flat_renderer.begin();

		flat_renderer.render_text(GRANITE_UI_MANAGER()->get_font(UI::FontSize::Large),
		                          "Hello Granite",
		                          vec3(10.0f, 10.0f, 0.0f),
		                          vec2(1000.0f),
		                          vec4(1.0f, 0.0f, 1.0f, 1.0f));

		// The camera_pos and camera_size denote the canvas size.
		// We work in pixel units mostly, so using the viewport size as a baseline is a good default.
		// The Z dimension denotes how we subdivide the depth plane.
		// 2D objects also have depth and make use of the depth buffer (opaque 2D objects).
		flat_renderer.flush(
				*cmd, vec3(0.0f),
				vec3(cmd->get_viewport().width, cmd->get_viewport().height, 1.0f));

		cmd->end_render_pass();
		device.submit(cmd);
	}

	// Modify these as desired. For now, just call into the parent,
	// so it's effectively the same as not overriding.
	// This code is only here for demonstration purposes.
	void post_frame() override
	{
		Application::post_frame();
	}

	void render_early_loading(double frame_time, double elapsed_time) override
	{
		Application::render_early_loading(frame_time, elapsed_time);
	}

	void render_loading(double frame_time, double elapsed_time) override
	{
		Application::render_loading(frame_time, elapsed_time);
	}

	FPSCamera fps_camera;
	RenderContext context;
	LightingParameters lighting = {};
	SceneLoader scene_loader;
	FlatRenderer flat_renderer;
	Renderer renderer;
	RenderQueue queue;
	VisibilityList visible;
};

namespace Granite
{
Application *application_create(int argc, char **argv)
{
	GRANITE_APPLICATION_SETUP_FILESYSTEM();

	std::string path;
	if (argc >= 2)
		path = argv[1];
	else
		path = "assets://scene.glb";

	LOGI("Loading glTF file from %s.\n", path.c_str());

	try
	{
		auto *app = new ViewerApplication(path);
		return app;
	}
	catch (const std::exception &e)
	{
		LOGE("application_create() threw exception: %s\n", e.what());
		return nullptr;
	}
}
}
