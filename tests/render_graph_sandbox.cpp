/* Copyright (c) 2017-2020 Hans-Kristian Arntzen
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
#include "render_graph.hpp"
#include "os_filesystem.hpp"
#include "task_composer.hpp"
#include <string.h>

using namespace Granite;
using namespace Vulkan;

struct RenderGraphSandboxApplication : Granite::Application, Granite::EventHandler
{
	RenderGraphSandboxApplication()
	{
		EVENT_MANAGER_REGISTER_LATCH(RenderGraphSandboxApplication, on_swapchain_created, on_swapchain_destroyed, SwapchainParameterEvent);
	}

	void on_swapchain_created(const SwapchainParameterEvent &e)
	{
		graph.reset();
		graph.set_device(&e.get_device());

		ResourceDimensions dim;
		dim.width = e.get_width();
		dim.height = e.get_height();
		dim.format = e.get_format();
		dim.transform = e.get_prerotate();
		graph.set_backbuffer_dimensions(dim);

		AttachmentInfo back;

		AttachmentInfo im;
		im.format = VK_FORMAT_R8G8B8A8_UNORM;
		im.size_x = 1280.0f;
		im.size_y = 720.0f;
		im.size_class = SizeClass::Absolute;

		// Pretend depth pass.
		auto &depth = graph.add_pass("depth", RENDER_GRAPH_QUEUE_GRAPHICS_BIT);
		depth.add_color_output("depth", back);

		depth.set_get_clear_color([](unsigned, VkClearColorValue *value) -> bool {
			if (value)
			{
				value->float32[0] = 0.0f;
				value->float32[1] = 1.0f;
				value->float32[2] = 0.0f;
				value->float32[3] = 1.0f;
			}
			return true;
		});

		depth.set_build_render_pass([&](Vulkan::CommandBuffer &cmd) {
			CommandBufferUtil::setup_fullscreen_quad(cmd, "builtin://shaders/quad.vert",
			                                         "assets://shaders/additive.frag");
			cmd.set_blend_enable(true);
			cmd.set_blend_factors(VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
			CommandBufferUtil::draw_fullscreen_quad(cmd, 20);
		});

		// Pretend main rendering pass.
		auto &graphics = graph.add_pass("first", RENDER_GRAPH_QUEUE_GRAPHICS_BIT);
		auto &first = graphics.add_color_output("first", back);
		graphics.add_texture_input("depth");
		graphics.set_get_clear_color([](unsigned, VkClearColorValue *value) -> bool {
			if (value)
			{
				value->float32[0] = 1.0f;
				value->float32[1] = 0.0f;
				value->float32[2] = 1.0f;
				value->float32[3] = 1.0f;
			}
			return true;
		});

		graphics.set_build_render_pass([&](Vulkan::CommandBuffer &cmd) {
			CommandBufferUtil::setup_fullscreen_quad(cmd, "builtin://shaders/quad.vert",
			                                         "assets://shaders/additive.frag");
			cmd.set_blend_enable(true);
			cmd.set_blend_factors(VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
			CommandBufferUtil::draw_fullscreen_quad(cmd, 80);
		});

		// Post processing
		auto &compute = graph.add_pass("compute", RENDER_GRAPH_QUEUE_ASYNC_COMPUTE_BIT);
		auto &i = compute.add_storage_texture_output("image", im);
		compute.add_texture_input("first");
		compute.set_build_render_pass([&](Vulkan::CommandBuffer &cmd) {
			auto &device = get_wsi().get_device();
			auto *program = device.get_shader_manager().register_compute("assets://shaders/image_write.comp");
			auto *variant = program->register_variant({});
			cmd.set_program(variant->get_program());
			cmd.set_storage_texture(0, 0, graph.get_physical_texture_resource(i));
			cmd.set_texture(0, 1, graph.get_physical_texture_resource(first), StockSampler::LinearClamp);
			cmd.dispatch(1280 / 8, 720 / 8, 40);
		});

		// Composite + UI
		auto &swap = graph.add_pass("final", RENDER_GRAPH_QUEUE_ASYNC_GRAPHICS_BIT);
		swap.add_color_output("back", back);
		swap.add_texture_input("image");
		swap.add_texture_input("first");
		swap.set_build_render_pass([&](Vulkan::CommandBuffer &cmd) {
			cmd.set_texture(0, 0, graph.get_physical_texture_resource(i), StockSampler::LinearClamp);
			cmd.set_texture(0, 1, graph.get_physical_texture_resource(first), StockSampler::LinearClamp);
			CommandBufferUtil::draw_fullscreen_quad(cmd, "builtin://shaders/quad.vert", "builtin://shaders/blit.frag");
		});

		graph.set_backbuffer_source("back");
		graph.bake();
		graph.log();
	}

	void on_swapchain_destroyed(const SwapchainParameterEvent &)
	{
	}

	void render_frame(double, double)
	{
		auto &wsi = get_wsi();
		auto &device = wsi.get_device();
		graph.setup_attachments(device, &device.get_swapchain_view());
		TaskComposer composer(*Global::thread_group());
		graph.enqueue_render_passes(device, composer);
		composer.get_outgoing_task()->wait();
	}

	RenderGraph graph;
};

namespace Granite
{
Application *application_create(int, char **)
{
	application_dummy();

#ifdef ASSET_DIRECTORY
	const char *asset_dir = getenv("ASSET_DIRECTORY");
	if (!asset_dir)
		asset_dir = ASSET_DIRECTORY;

	Global::filesystem()->register_protocol("assets", std::unique_ptr<FilesystemBackend>(new OSFilesystem(asset_dir)));
#endif

	try
	{
		auto *app = new RenderGraphSandboxApplication();
		return app;
	}
	catch (const std::exception &e)
	{
		LOGE("application_create() threw exception: %s\n", e.what());
		return nullptr;
	}
}
}
