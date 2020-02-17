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
#include "command_buffer.hpp"
#include "device.hpp"
#include "os_filesystem.hpp"
#include "muglm/muglm_impl.hpp"
#include <string.h>
#include <flat_renderer.hpp>
#include <ui_manager.hpp>

using namespace Granite;
using namespace Vulkan;

struct PresentModeTest : Granite::Application, Granite::EventHandler
{
	PresentModeTest(int iterations_)
		: iterations(iterations_)
	{
		EVENT_MANAGER_REGISTER(PresentModeTest, on_key_pressed, KeyboardEvent);
		EVENT_MANAGER_REGISTER_LATCH(PresentModeTest, on_swapchain_created, on_swapchain_destroyed, SwapchainParameterEvent);
		EVENT_MANAGER_REGISTER(PresentModeTest, on_input_state, InputStateEvent);
		frame_time_history.reserve(1024);
	}

	bool on_input_state(const InputStateEvent &e)
	{
		if (e.get_key_pressed(Key::Left))
			pos_x -= 0.5f * e.get_delta_time();
		if (e.get_key_pressed(Key::Right))
			pos_x += 0.5f * e.get_delta_time();

		if (e.get_key_pressed(Key::Up))
			pos_y -= 0.5f * e.get_delta_time();
		if (e.get_key_pressed(Key::Down))
			pos_y += 0.5f * e.get_delta_time();

		pos_x = clamp(pos_x, aspect_ratio * -0.5f, aspect_ratio * 0.5f);
		pos_y = clamp(pos_y, -0.5f, 0.5f);
		return true;
	}

	bool on_key_pressed(const KeyboardEvent &e)
	{
		auto &wsi = get_wsi();
		if (e.get_key_state() != KeyState::Pressed)
			return true;

		if (e.get_key() == Key::Space)
			wsi.set_present_mode(wsi.get_present_mode() == PresentMode::SyncToVBlank ? PresentMode::UnlockedForceTearing : PresentMode::SyncToVBlank);

		return true;
	}

	void on_swapchain_created(const SwapchainParameterEvent &e)
	{
		aspect_ratio = e.get_aspect_ratio();
	}

	void on_swapchain_destroyed(const SwapchainParameterEvent &)
	{
	}

	void render_dummy_async_compute()
	{
		auto &wsi = get_wsi();
		auto &device = wsi.get_device();
		auto cmd = device.request_command_buffer(CommandBuffer::Type::AsyncCompute);

		BufferCreateInfo info = {};
		info.size = 4 * 1024 * 1024;
		info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		auto buffer = device.create_buffer(info);
		cmd->fill_buffer(*buffer, 0);
		device.submit(cmd);
	}

	void render_frame(double frame_time, double) override
	{
		auto &wsi = get_wsi();
		auto &device = wsi.get_device();

		render_dummy_async_compute();

		if (frame_time_history.size() > 64)
			frame_time_history.erase(frame_time_history.begin());
		frame_time_history.push_back(float(frame_time));

		auto cmd = device.request_command_buffer();
		auto rp = device.get_swapchain_render_pass(SwapchainRenderPass::ColorOnly);
		rp.clear_color[0].float32[0] = 0.01f;
		rp.clear_color[0].float32[1] = 0.02f;
		rp.clear_color[0].float32[2] = 0.03f;
		cmd->begin_render_pass(rp);

		cmd->set_opaque_state();
		cmd->set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
		cmd->set_vertex_attrib(0, 0, VK_FORMAT_R32G32_SFLOAT, 0);
		cmd->set_vertex_attrib(1, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0);

		cmd->set_program("assets://shaders/triangle.vert", "assets://shaders/triangle.frag");
		cmd->set_specialization_constant_mask(1);
		cmd->set_specialization_constant(0, iterations);

		mat2 scale_offset = mat2(vec2(1.0f / aspect_ratio, 1.0f), vec2(pos_x, pos_y));
		cmd->push_constants(&scale_offset, 0, sizeof(scale_offset));

		static const vec2 vertices[] = {
			vec2(-0.7f, -0.7f),
			vec2(-0.7f, +0.7f),
			vec2(+0.7f, -0.7f),
			vec2(+0.7f, +0.7f),
		};
		auto *verts = static_cast<vec2 *>(cmd->allocate_vertex_data(0, sizeof(vertices), sizeof(vec2)));
		memcpy(verts, vertices, sizeof(vertices));

		static const vec4 colors[] = {
			vec4(1.0f, 0.0f, 0.0f, 1.0f),
			vec4(0.0f, 1.0f, 0.0f, 1.0f),
			vec4(0.0f, 0.0f, 1.0f, 1.0f),
			vec4(0.0f, 0.0f, 0.0f, 1.0f),
		};
		auto *col = static_cast<vec4 *>(cmd->allocate_vertex_data(1, sizeof(colors), sizeof(vec4)));
		memcpy(col, colors, sizeof(colors));

		cmd->draw(4);
		cmd->set_specialization_constant_mask(0);

		draw_frame_time_history(*cmd);

		cmd->end_render_pass();
		device.submit(cmd);
	}

	static float convert_to_y(float t, float min_y, float max_y)
	{
		float l = (t - min_y) / muglm::max(0.00000001f, max_y - min_y);
		return 0.88f - 0.16f * l;
	}

	void draw_frame_time_history(CommandBuffer &cmd)
	{
		renderer.begin();
		float width = cmd.get_viewport().width;
		float height = cmd.get_viewport().height;

		if (!frame_time_history.empty())
		{
			renderer.render_quad(vec3(width * 0.1f, height * 0.7f, 1.0f),
			                     vec2(width * 0.8f, height * 0.2f),
			                     vec4(0.0f, 0.0f, 0.0f, 0.8f));

			float x_start = 0.15f;
			float x_inc = 0.7f / float(frame_time_history.size() - 1);

			float max_y = 0.0f;
			for (auto t : frame_time_history)
				max_y = muglm::max(t, max_y);

			float min_y = std::numeric_limits<float>::max();
			for (auto t : frame_time_history)
				min_y = muglm::min(t, min_y);

			std::vector<vec2> line_strip(frame_time_history.size());
			for (size_t i = 0; i < frame_time_history.size(); i++)
			{
				line_strip[i] = vec2(width * x_start, height * convert_to_y(frame_time_history[i], min_y, max_y));
				x_start += x_inc;
			}
			renderer.render_line_strip(line_strip.data(), 0.5f, line_strip.size(), vec4(1.0f));

			char text[256];
			sprintf(text, "Min frame time: %.3f ms", min_y * 1000.0f);
			renderer.render_text(GRANITE_UI_MANAGER()->get_font(UI::FontSize::Normal),
			                     text, vec3(0.11f * width, 0.71f * height, 0.0f), vec2(0.78f * width, 0.18f * height),
			                     vec4(1.0f), Font::Alignment::BottomLeft);

			sprintf(text, "Max frame time: %.3f ms", max_y * 1000.0f);
			renderer.render_text(GRANITE_UI_MANAGER()->get_font(UI::FontSize::Normal),
			                     text, vec3(0.11f * width, 0.71f * height, 0.0f), vec2(0.78f * width, 0.18f * height),
			                     vec4(1.0f), Font::Alignment::TopLeft);
		}

		renderer.flush(cmd, vec3(0.0f), vec3(width, height, 1.0f));
	}

	float pos_x = 0.0f, pos_y = 0.0f;
	float aspect_ratio = 1.0f;
	int iterations;
	std::vector<float> frame_time_history;
	FlatRenderer renderer;
};

namespace Granite
{
Application *application_create(int argc, char **argv)
{
	GRANITE_APPLICATION_SETUP_FILESYSTEM();

	int iterations = 1000;
	if (argc == 2)
		iterations = atoi(argv[1]);

	try
	{
		auto *app = new PresentModeTest(iterations);
		return app;
	}
	catch (const std::exception &e)
	{
		LOGE("application_create() threw exception: %s\n", e.what());
		return nullptr;
	}
}
}