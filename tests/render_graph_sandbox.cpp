/* Copyright (c) 2017-2018 Hans-Kristian Arntzen
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
#include "os.hpp"
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
		graph.set_backbuffer_dimensions(dim);

		AttachmentInfo im;
		im.format = VK_FORMAT_R8G8B8A8_UNORM;

		auto &compute = graph.add_pass("compute", RENDER_GRAPH_QUEUE_ASYNC_COMPUTE_BIT);
		auto &i = compute.add_storage_texture_output("image", im);
		compute.set_build_render_pass([&](Vulkan::CommandBuffer &cmd) {
			auto &device = get_wsi().get_device();
			auto *program = device.get_shader_manager().register_compute("assets://shaders/image_write.comp");
			unsigned variant = program->register_variant({});
			cmd.set_program(*program->get_program(variant));
			cmd.set_storage_texture(0, 0, graph.get_physical_texture_resource(i.get_physical_index()));
			cmd.dispatch(1280 / 8, 720 / 8, 1);
		});

		graph.set_backbuffer_source("image");
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
		graph.enqueue_render_passes(device);
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

	Filesystem::get().register_protocol("assets", std::unique_ptr<FilesystemBackend>(new OSFilesystem(asset_dir)));
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
