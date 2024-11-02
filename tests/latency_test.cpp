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
#include "command_buffer.hpp"
#include "device.hpp"
#include "os_filesystem.hpp"
#include "font.hpp"
#include "ui_manager.hpp"
#include "string_helpers.hpp"
#include "global_managers.hpp"
#include "flat_renderer.hpp"
#include "application_events.hpp"

using namespace Granite;
using namespace Vulkan;

struct LatencyTest : Granite::Application, Granite::EventHandler
{
	explicit LatencyTest(unsigned count_)
		: count(count_)
	{
		EVENT_MANAGER_REGISTER(LatencyTest, on_key_down, KeyboardEvent);
		frame_times.reserve(100);
		get_wsi().set_low_latency_mode(true);
	}

	bool on_key_down(const KeyboardEvent &e)
	{
		if (e.get_key_state() == KeyState::Pressed && e.get_key() == Key::Space)
			state = !state;
		return true;
	}

	void render_frame(double frame_time, double elapsed_time) override
	{
		auto &wsi = get_wsi();
		auto &device = wsi.get_device();

		if (frame_times.empty())
			frame_times.resize(100, frame_time);
		else
		{
			if (frame_times.size() >= 100)
				frame_times.erase(frame_times.begin());
			frame_times.push_back(frame_time);
		}

		double min_time = std::numeric_limits<double>::max();
		double max_time = 0.0;
		double avg_time = 0.0;
		for (auto &t : frame_times)
		{
			min_time = std::min(min_time, t);
			max_time = std::max(max_time, t);
			avg_time += t;
		}
		avg_time /= double(frame_times.size());

		auto cmd = device.request_command_buffer();
		auto rp = device.get_swapchain_render_pass(SwapchainRenderPass::ColorOnly);

		if (state)
		{
			rp.clear_color[0].float32[0] = 0.1f;
			rp.clear_color[0].float32[1] = 0.2f;
			rp.clear_color[0].float32[2] = 0.3f;
		}
		else
		{
			rp.clear_color[0].float32[0] = 0.3f;
			rp.clear_color[0].float32[1] = 0.2f;
			rp.clear_color[0].float32[2] = 0.1f;
		}

		cmd->begin_render_pass(rp);
		auto start_ts = cmd->write_timestamp(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

		flat.begin();

		for (unsigned i = 0; i < count; i++)
		{
			flat.render_quad({0.0f, 0.0f, 4.0f}, {cmd->get_viewport().width, cmd->get_viewport().height},
			                 { 1.0f, 0.0f, 0.0f, 2.0f / 255.0f });
		}

		char avg_text[1024], min_text[1024], max_text[1024];
		vec3 offset = { 10.0f, 10.0f, 0.0f };
		vec2 size = { cmd->get_viewport().width - 20.0f, cmd->get_viewport().height - 20.0f };
		snprintf(avg_text, sizeof(avg_text), "Average frame time: %.3f ms", 1000.0 * avg_time);
		snprintf(min_text, sizeof(min_text), "Minimum frame time: %.3f ms", 1000.0 * min_time);
		snprintf(max_text, sizeof(max_text), "Maximum frame time: %.3f ms", 1000.0 * max_time);
		flat.render_text(GRANITE_UI_MANAGER()->get_font(UI::FontSize::Large),
		                 avg_text, offset, size, vec4(1.0f, 1.0f, 0.0f, 1.0f),
		                 Font::Alignment::TopRight);
		offset.y += 30.0f;
		flat.render_text(GRANITE_UI_MANAGER()->get_font(UI::FontSize::Normal),
		                 min_text, offset, size, vec4(1.0f, 1.0f, 0.0f, 1.0f),
		                 Font::Alignment::TopRight);
		offset.y += 30.0f;
		flat.render_text(GRANITE_UI_MANAGER()->get_font(UI::FontSize::Normal),
		                 max_text, offset, size, vec4(1.0f, 1.0f, 0.0f, 1.0f),
		                 Font::Alignment::TopRight);

		offset = { cmd->get_viewport().width - 410.0f, cmd->get_viewport().height - 110.0f, 0.0f };
		size = { 400.0f, 100.0f };
		flat.render_quad(offset, size, vec4(0.0f, 0.0f, 0.0f, 0.9f));

		vec2 offsets[100];

		const auto remap_range = [&](double t) -> float {
			if (t == min_time)
				return 0.0f;
			else if (t == max_time)
				return 1.0f;
			else
				return float((t - min_time) / (max_time - min_time));
		};

		for (unsigned i = 0; i < 100; i++)
		{
			offsets[i].x = offset.x + float(i) / (100.0f - 1.0f) * size.x;
			offsets[i].y = offset.y + size.y;
			offsets[i].y -= remap_range(frame_times[i]) * size.y;
		}
		flat.render_line_strip(offsets, 0.0f, 100, vec4(1.0f, 1.0f, 0.0f, 1.0f));

		char elapsed_text[256];
		snprintf(elapsed_text, sizeof(elapsed_text), "Elapsed: %.3f, Frame: %u", elapsed_time, counter++);
		flat.render_text(GRANITE_UI_MANAGER()->get_font(UI::FontSize::Large),
						 elapsed_text, { 0, 0, 0 }, { cmd->get_viewport().width, cmd->get_viewport().height },
						 vec4(1.0f), Font::Alignment::Center);

		flat.flush(*cmd, vec3(0.0f), { cmd->get_viewport().width, cmd->get_viewport().height, 5.0f });

		cmd->end_render_pass();
		auto end_ts = cmd->write_timestamp(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
		device.register_time_interval("GPU", std::move(start_ts), std::move(end_ts), "RenderPass");
		device.submit(cmd);
	}

	unsigned counter = 0;
	std::vector<double> frame_times;
	bool state = false;
	FlatRenderer flat;
	unsigned count;
};

namespace Granite
{
Application *application_create(int argc, char **argv)
{
	application_setup_default_filesystem(".");

	unsigned count = 0;
	if (argc == 2)
		count = strtoul(argv[1], nullptr, 0);

	try
	{
		auto *app = new LatencyTest(count);
		return app;
	}
	catch (const std::exception &e)
	{
		LOGE("application_create() threw exception: %s\n", e.what());
		return nullptr;
	}
}
}
