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
#include "os_filesystem.hpp"
#include "mesh_util.hpp"
#include <string.h>
#include "device.hpp"

using namespace Granite;
using namespace Vulkan;
using namespace Util;
using namespace std;

struct ClusteringVizApplication : Granite::Application, Granite::EventHandler
{
	ClusteringVizApplication()
	{
		cube = CubeMesh::build_plain_mesh();
		EVENT_MANAGER_REGISTER(ClusteringVizApplication, on_key, KeyboardEvent);
	}

	bool on_key(const KeyboardEvent &e)
	{
		if (e.get_key() == Key::C && e.get_key_state() == KeyState::Pressed)
			should_cull = !should_cull;
		return true;
	}

	void render_frame(double, double)
	{
		auto &wsi = get_wsi();
		auto &device = wsi.get_device();
		auto cmd = device.request_command_buffer();
		context.set_camera(cam);

		auto rp = device.get_swapchain_render_pass(SwapchainRenderPass::Depth);
		rp.clear_color[0].float32[0] = 0.1f;
		rp.clear_color[0].float32[1] = 0.2f;
		rp.clear_color[0].float32[2] = 0.3f;
		cmd->begin_render_pass(rp);

		cmd->set_opaque_state();
		cmd->set_primitive_topology(cube.topology);
		cmd->set_vertex_attrib(0, 0, cube.attribute_layout[ecast(MeshAttribute::Position)].format, 0);
		cmd->set_vertex_attrib(1, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0);
		cmd->set_vertex_attrib(2, 1, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(vec4));
		memcpy(cmd->allocate_vertex_data(0, cube.positions.size(), cube.position_stride), cube.positions.data(), cube.positions.size());
		memcpy(cmd->allocate_index_data(cube.indices.size(), cube.index_type), cube.indices.data(), cube.indices.size());

		auto *program = device.get_shader_manager().register_graphics("assets://shaders/clustering_viz.vert", "assets://shaders/clustering_viz.frag");
		auto *variant = program->register_variant({});
		cmd->set_program(variant->get_program());

		static const unsigned res_x = 64 / 4;
		static const unsigned res_y = 32 / 4;
		static const unsigned res_z = 16 / 4;
		static const unsigned instances = res_x * res_y * res_z * 5;

		static const vec3 colors[5] = {
			vec3(1.0f, 0.0f, 0.0f),
			vec3(0.0f, 1.0f, 0.0f),
			vec3(0.0f, 0.0f, 1.0f),
			vec3(1.0f, 1.0f, 0.0f),
			vec3(1.0f, 0.0f, 1.0f),
		};

		struct Cube
		{
			vec4 pos;
			vec4 color;
		};
		vector<Cube> cubes;
		cubes.reserve(instances);
		for (unsigned level = 0; level < 5; level++)
		{
			unsigned scale_level_z = level == 0 ? 0 : (level - 1);
			float scale = 0.25f * float(1u << scale_level_z);
			for (unsigned z = 0; z < res_z; z++)
			{
				float w = ((level == 0 ? z : (z + res_z)) + 0.5f) / (2.0f * res_z);
				for (unsigned y = 0; y < res_y; y++)
				{
					float v = 2.0f * ((y + 0.5f) / res_y) - 1.0f;
					if (should_cull && (fabs(v) > w))
						continue;
					for (unsigned x = 0; x < res_x; x++)
					{
						float u = 2.0f * (2.0f * ((x + 0.5f) / res_x) - 1.0f);
						if (should_cull && (fabs(0.5f * u) > w))
							continue;
						cubes.push_back({
							vec4(scale * u, scale * v, -scale * w, scale / res_y),
							vec4(colors[level], float(level)),
						});
					}
				}
			}
		}

		stable_sort(begin(cubes), end(cubes), [&](const Cube &a, const Cube &b) -> bool {
			const vec3 &pos = context.get_render_parameters().camera_position;
			float a_sqr = dot(a.pos.xyz() - pos, a.pos.xyz() - pos);
			float b_sqr = dot(b.pos.xyz() - pos, b.pos.xyz() - pos);
			return a_sqr < b_sqr;
		});

		memcpy(cmd->allocate_vertex_data(1, cubes.size() * sizeof(Cube), sizeof(Cube), VK_VERTEX_INPUT_RATE_INSTANCE),
		       cubes.data(),
		       cubes.size() * sizeof(Cube));

		cmd->push_constants(&context.get_render_parameters().view_projection, 0, sizeof(mat4));
		//cmd->set_blend_enable(true);
		//cmd->set_depth_test(false, false);
		//cmd->set_blend_factors(VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);

		cmd->draw_indexed(cube.count, cubes.size());
		cmd->end_render_pass();
		device.submit(cmd);
	}

	SceneFormats::Mesh cube;
	FPSCamera cam;
	RenderContext context;
	bool should_cull = false;
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
		auto *app = new ClusteringVizApplication();
		return app;
	}
	catch (const std::exception &e)
	{
		LOGE("application_create() threw exception: %s\n", e.what());
		return nullptr;
	}
}
}