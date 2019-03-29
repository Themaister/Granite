/* Copyright (c) 2017-2019 Hans-Kristian Arntzen
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
#include "command_buffer.hpp"
#include "device.hpp"
#include "os_filesystem.hpp"
#include "math.hpp"
#include "bitmap_to_mesh.hpp"
#include "muglm/muglm_impl.hpp"
#include <string.h>

using namespace Granite;
using namespace Vulkan;

struct BitmapMeshApplication : Granite::Application, Granite::EventHandler
{
	void render_frame(double, double)
	{
		auto &wsi = get_wsi();
		auto &device = wsi.get_device();
		auto cmd = device.request_command_buffer();

		auto rp = device.get_swapchain_render_pass(SwapchainRenderPass::Depth);
		rp.clear_color[0].float32[0] = 0.1f;
		rp.clear_color[0].float32[1] = 0.2f;
		rp.clear_color[0].float32[2] = 0.3f;
		cmd->begin_render_pass(rp);

		VoxelizedBitmap bitmap;
#define O 0xff
#define x 0x00
		static const uint8_t comps[] = {
			x, x, x, O, O, x, x, x,
			x, x, O, O, O, O, x, x,
			x, O, O, O, x, O, O, x,
			O, O, O, x, x, O, O, O,
			O, O, O, x, x, O, O, O,
			x, O, O, O, O, O, O, x,
			x, x, O, O, O, O, x, x,
			x, x, x, O, O, x, x, x,
		};
#undef O
#undef x
		voxelize_bitmap(bitmap, comps, 0, 1, 8, 8, 8);

		Camera cam;
		cam.set_aspect(16.0f / 9.0f);
		cam.set_depth_range(1.0f, 100.0f);
		cam.set_fovy(0.4f * pi<float>());
		cam.look_at(vec3(4.0f, 5.0f, 4.0f), vec3(4.0f, 0.0f, 4.0f), vec3(0.0f, 0.0f, -1.0f));

		mat4 vp = cam.get_projection() * cam.get_view();
		cmd->push_constants(&vp, 0, sizeof(vp));

		cmd->set_opaque_state();
		cmd->set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
		cmd->set_wireframe(true);
		memcpy(cmd->allocate_vertex_data(0, bitmap.positions.size() * sizeof(vec3), sizeof(vec3)),
		       bitmap.positions.data(),
		       bitmap.positions.size() * sizeof(vec3));
		cmd->set_vertex_attrib(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0);
		cmd->set_program("assets://shaders/bitmap_mesh.vert", "assets://shaders/bitmap_mesh.frag");
		cmd->draw(bitmap.positions.size());

		cmd->end_render_pass();
		device.submit(cmd);
	}
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
		auto *app = new BitmapMeshApplication();
		return app;
	}
	catch (const std::exception &e)
	{
		LOGE("application_create() threw exception: %s\n", e.what());
		return nullptr;
	}
}
}