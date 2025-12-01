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

#define WAIT_IDLE 0

struct LatencyTest : Granite::Application, Granite::EventHandler
{
	explicit LatencyTest(unsigned count_)
		: count(count_)
	{
		EVENT_MANAGER_REGISTER(LatencyTest, on_key_down, KeyboardEvent);
		EVENT_MANAGER_REGISTER_LATCH(LatencyTest, on_device_created, on_device_destroyed, DeviceCreatedEvent);
		frame_times.reserve(100);
		get_wsi().set_gpu_submit_low_latency_mode(true);
	}

	bool gpu_low_latency_state = true;

	void on_device_created(const DeviceCreatedEvent &e)
	{
		(void)e;
		//e.get_device().init_frame_contexts(3);
	}

	void on_device_destroyed(const DeviceCreatedEvent &)
	{
	}

	bool on_key_down(const KeyboardEvent &e)
	{
		if (e.get_key_state() == KeyState::Pressed && e.get_key() == Key::Space)
			state = !state;
		if (e.get_key_state() == KeyState::Pressed && e.get_key() == Key::L)
		{
			gpu_low_latency_state = !gpu_low_latency_state;
			get_wsi().set_gpu_submit_low_latency_mode(gpu_low_latency_state);
		}
		return true;
	}

	void burn_compute()
	{
		auto &wsi = get_wsi();
		auto &device = wsi.get_device();
		auto cmd = device.request_command_buffer(CommandBuffer::Type::AsyncCompute);

		BufferCreateInfo info = {};
		info.size = 64 * 1024;
		info.domain = BufferDomain::Device;
		info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		auto buf = device.create_buffer(info);

		const uint32_t burn_count = 20000;
		cmd->push_constants(&burn_count, 0, sizeof(burn_count));
		cmd->set_program("assets://shaders/burn.comp");
		cmd->set_storage_buffer(0, 0, *buf);
		auto start_ts = cmd->write_timestamp(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
		cmd->dispatch(1, 1, 1);
		auto end_ts = cmd->write_timestamp(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

		device.register_time_interval("GPU", std::move(start_ts), std::move(end_ts), "Compute Burn");

		Semaphore sem;
		device.submit(cmd, nullptr, 1, &sem);
		device.add_wait_semaphore(CommandBuffer::Type::Generic, std::move(sem), VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, true);
	}

	uint64_t last_prediction = 0;

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

		RefreshRateInfo refresh_info;
		PresentationStats stats;

		if (wsi.get_presentation_stats(stats) && wsi.get_refresh_rate_info(refresh_info))
		{
			LOGI("VRR: %u\n", refresh_info.mode == RefreshMode::VRR ? 1 : 0);

			uint64_t expected_duration = 0;
			if (refresh_info.refresh_interval != UINT64_MAX && refresh_info.refresh_interval != 0)
				expected_duration = refresh_info.refresh_interval;
			else
				expected_duration = refresh_info.refresh_duration;

			expected_duration *= 2;

			// Relative time test.
			wsi.set_target_presentation_time(0, expected_duration);

			if (expected_duration)
			{
				uint64_t prediction =
						(1 + stats.last_submitted_present_id - stats.feedback_present_id) *
						expected_duration + stats.present_done_ts;

				prediction = std::max<uint64_t>(prediction, last_prediction + expected_duration);
				last_prediction = prediction;

				// Absolute test.
				//wsi.set_target_presentation_time(prediction, 0);

				LOGI("Current time: %.3f, estimating present ID %llu to complete at %.3f s.\n",
					 1e-9 * double(Util::get_current_time_nsecs()),
					 static_cast<unsigned long long>(stats.last_submitted_present_id + 1),
					 1e-9 * double(prediction));

				LOGI("  Next submit ID %llu, known presentID %llu, done %.3f s.\n",
					 static_cast<unsigned long long>(stats.last_submitted_present_id + 1),
					 static_cast<unsigned long long>(stats.feedback_present_id),
					 1e-9 * double(stats.present_done_ts));
			}
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

		burn_compute();

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

		const uint32_t burn_count = 1000;
		cmd->push_constants(&burn_count, 0, sizeof(burn_count));
		CommandBufferUtil::draw_fullscreen_quad(*cmd, "builtin://shaders/quad.vert", "assets://shaders/burn.frag");

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

#if WAIT_IDLE
		Fence fence;
		device.submit(cmd, &fence);
		fence->wait();
#else
		device.submit(cmd);
#endif
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
	GRANITE_APPLICATION_SETUP_FILESYSTEM();

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
