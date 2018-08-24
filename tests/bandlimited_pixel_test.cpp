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
#include "os.hpp"
#include "muglm/matrix_helper.hpp"
#include <string.h>

using namespace Granite;
using namespace Vulkan;

struct BandlimitedPixelTestApplication : Application, EventHandler
{
	BandlimitedPixelTestApplication()
	{
		EVENT_MANAGER_REGISTER_LATCH(BandlimitedPixelTestApplication, on_swapchain_created, on_swapchain_destroyed, SwapchainParameterEvent);
		cam.look_at(vec3(0.0f, 0.0f, 5.0f), vec3(0.0f));
	}

	void on_swapchain_created(const SwapchainParameterEvent &e)
	{
		cam.set_aspect(e.get_aspect_ratio());
		cam.set_fovy(half_pi<float>());
		cam.set_depth_range(0.05f, 100.0f);
	}

	void on_swapchain_destroyed(const SwapchainParameterEvent &)
	{
	}

	void render_frame(double, double elapsed)
	{
		auto &wsi = get_wsi();
		auto &device = wsi.get_device();

		auto cmd = device.request_command_buffer();

		auto rp = device.get_swapchain_render_pass(SwapchainRenderPass::ColorOnly);
		rp.clear_color[0].float32[0] = 0.1f;
		rp.clear_color[0].float32[1] = 0.2f;
		rp.clear_color[0].float32[2] = 0.3f;
		cmd->begin_render_pass(rp);
		cmd->set_opaque_state();
		cmd->set_cull_mode(VK_CULL_MODE_NONE);

		cmd->set_program("assets://shaders/bandlimited_quad.vert",
		                 "builtin://shaders/sprite.frag",
		                 {{ "HAVE_BASECOLORMAP", 1 },
		                  { "HAVE_VERTEX_COLOR", 1 },
		                  { "HAVE_UV", 1 },
		                  { "VARIANT_BIT_0", 1 }});

		auto *texture = device.get_texture_manager().request_texture("assets://textures/sprite.png");
		cmd->set_texture(2, 0, texture->get_image()->get_view(), StockSampler::TrilinearWrap);

		CommandBufferUtil::set_quad_vertex_state(*cmd);

		quat rot = angleAxis(float(elapsed * 0.25), vec3(0.0f, 0.0f, 1.0f));

		mat4 mvp = cam.get_projection() * cam.get_view() * mat4_cast(rot) * scale(10.0f * vec3(320.0f / 200.0f, 1.0f, 1.0f));
		cmd->push_constants(&mvp, 0, sizeof(mvp));

		CommandBufferUtil::draw_quad(*cmd);
		cmd->end_render_pass();
		device.submit(cmd);
	}

	FPSCamera cam;
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
		auto *app = new BandlimitedPixelTestApplication();
		return app;
	}
	catch (const std::exception &e)
	{
		LOGE("application_create() threw exception: %s\n", e.what());
		return nullptr;
	}
}
}