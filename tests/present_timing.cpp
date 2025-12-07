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

static constexpr unsigned WindowSize = 100;

template <typename Itr>
static auto minmax_range(Itr begin, Itr end) -> std::pair<std::decay_t<decltype(*begin)>, std::decay_t<decltype(*begin)>>
{
	using T = std::decay_t<decltype(*begin)>;
	std::pair<T, T> res {std::numeric_limits<T>::max(), T(0)};

	while (begin != end)
	{
		res.first = std::min<T>(res.first, *begin);
		res.second = std::max<T>(res.second, *begin);
		++begin;
	}

	return res;
}

template <typename Itr>
static auto average_range(Itr begin, Itr end) -> std::decay_t<decltype(*begin)>
{
	using T = std::decay_t<decltype(*begin)>;
	T avg = {};

	size_t num = 0;
	while (begin != end)
	{
		avg += *begin;
		num++;
		++begin;
	}

	return avg / T(num);
}

template <typename Itr, typename Op>
static auto average_range(Itr begin, Itr end, const Op &op) -> decltype(op(*begin))
{
	using T = decltype(op(*begin));
	T avg = {};

	size_t num = 0;
	while (begin != end)
	{
		avg += op(*begin);
		num++;
		++begin;
	}

	return avg / T(num);
}

struct PresentTiming : Granite::Application, Granite::EventHandler
{
	explicit PresentTiming(unsigned count_)
		: count(count_)
	{
		EVENT_MANAGER_REGISTER(PresentTiming, on_key_down, KeyboardEvent);
		frame_times.reserve(WindowSize);
	}

	bool on_key_down(const KeyboardEvent &e)
	{
		if (e.get_key_state() == KeyState::Released)
			return true;

		switch (e.get_key())
		{
		case Key::Up:
			burn_count++;
			break;

		case Key::Down:
			if (burn_count)
				burn_count--;
			break;

		case Key::Right:
			cycles_num++;
			break;

		case Key::Left:
			if (cycles_num)
				cycles_num--;
			break;

		case Key::V:
			force_vrr_timing = !force_vrr_timing;
			break;

		case Key::T:
			timing_request = !timing_request;
			break;

		case Key::P:
			present_wait_low_latency = !present_wait_low_latency;
			get_wsi().set_present_low_latency_mode(present_wait_low_latency);
			break;

		case Key::L:
			gpu_submit_low_latency = !gpu_submit_low_latency;
			get_wsi().set_gpu_submit_low_latency_mode(gpu_submit_low_latency);
			break;

		case Key::R:
			relative_timing = !relative_timing;
			break;

		default:
			break;
		}

		return true;
	}

	struct QueryResult
	{
		uint64_t present_id;
		uint64_t cpu_time_submit;
		uint64_t queue_done;
		uint64_t present_done;
		int64_t error;
		double burn_time;
	};
	Util::SmallVector<QueryResult, WindowSize> retired_results;

	struct PendingQueryResult : QueryResult
	{
		QueryPoolHandle start, end;
		bool complete;
	};
	Util::SmallVector<PendingQueryResult, 16> queries;

	bool supports_request = false;
	bool timing_request = false;
	uint32_t burn_count = 1;
	RefreshRateInfo refresh_info = {};
	PresentationStats stats = {};
	bool force_vrr_timing = false;
	bool present_wait_low_latency = false;
	bool gpu_submit_low_latency = false;
	unsigned cycles_num = 8;
	unsigned cycles_den = 8;
	bool relative_timing = true;

	void retire_query(PendingQueryResult &query)
	{
		auto &device = get_wsi().get_device();
		query.queue_done = std::max<uint64_t>(device.convert_timestamp_to_absolute_nsec(*query.end), query.queue_done);
		query.burn_time = device.convert_device_timestamp_delta(query.start->get_timestamp_ticks(),
																query.end->get_timestamp_ticks());
		if (retired_results.size() >= 100)
			retired_results.erase(retired_results.begin());
		retired_results.push_back(query);
	}

	void poll_timestamps()
	{
		for (size_t i = 0; i < queries.size(); )
		{
			if (queries[i].complete && queries[i].start->is_signalled() && queries[i].end->is_signalled())
			{
				retire_query(queries[i]);
				queries[i] = std::move(queries.back());
				queries.pop_back();
			}
			else
			{
				i++;
			}
		}

		// Safety if something is not supported.
		if (queries.size() >= 16)
			queries.clear();
	}

	uint64_t absolute_timing_accumulator = 0;

	void poll_present_timing()
	{
		auto &wsi = get_wsi();

		if (!wsi.get_presentation_stats(stats) || !wsi.get_refresh_rate_info(refresh_info))
			return;

		for (auto &query : queries)
		{
			if (query.present_id == stats.feedback_present_id)
			{
				query.queue_done = stats.gpu_done_ts;
				query.present_done = stats.present_done_ts;
				query.error = stats.error;
			}

			// Possible that we skipped ahead in the feedback.
			if (stats.feedback_present_id >= query.present_id)
				query.complete = true;
		}

		uint64_t expected_duration = refresh_info.refresh_duration * cycles_num / cycles_den;

		if (relative_timing)
		{
			// Relative time test.
			if (timing_request)
				supports_request = wsi.set_target_presentation_time(0, expected_duration, force_vrr_timing);
			else
				supports_request = wsi.set_target_presentation_time(0, 0, false);
		}
		else if (timing_request)
		{
			if (expected_duration)
			{
				uint64_t lower_prediction =
						(1 + wsi.get_last_submitted_present_id() - stats.feedback_present_id) *
						expected_duration + stats.present_done_ts;

				absolute_timing_accumulator += expected_duration;
				absolute_timing_accumulator = std::max(absolute_timing_accumulator, lower_prediction);

				if (refresh_info.mode != RefreshMode::VRR && !force_vrr_timing && stats.present_done_ts)
				{
					// Align to nearest refresh cycle to avoid drift.
					auto cycles = (absolute_timing_accumulator - stats.present_done_ts + refresh_info.refresh_duration / 2) /
					              refresh_info.refresh_duration;
					absolute_timing_accumulator = stats.present_done_ts + cycles * refresh_info.refresh_duration;
				}

				supports_request = wsi.set_target_presentation_time(absolute_timing_accumulator, 0, force_vrr_timing);
			}
		}
	}

	void render_history(const double *times, size_t num_times, vec2 offset, vec2 size)
	{
		Util::SmallVector<vec2, WindowSize> offsets;
		offsets.resize(num_times);

		std::pair<double, double> minmax;

		if (refresh_info.refresh_duration)
		{
			minmax.first = 0.0;
			minmax.second = double(refresh_info.refresh_duration) * 4e-9;
		}
		else
		{
			minmax = minmax_range(times, times + num_times);
		}

		const auto remap_range = [&](double t) -> float {
			if (t <= minmax.first)
				return 0.0f;
			else if (t >= minmax.second)
				return 1.0f;
			else
				return float((t - minmax.first) / (minmax.second - minmax.first));
		};

		for (size_t i = 0; i < num_times; i++)
		{
			offsets[i].x = offset.x + float(i) / (float(num_times) - 1.0f) * size.x;
			offsets[i].y = offset.y + size.y;
			offsets[i].y -= remap_range(times[i]) * size.y;
		}

		flat.render_line_strip(offsets.data(), 0.0f, num_times, vec4(1.0f, 1.0f, 0.0f, 1.0f));
	}

	void render_frame(double frame_time, double elapsed_time) override
	{
		auto &wsi = get_wsi();
		auto &device = wsi.get_device();

		if (frame_times.empty())
		{
			for (unsigned i = 0; i < WindowSize; i++)
				frame_times.push_back(frame_time);
		}
		else
		{
			if (frame_times.size() >= 100)
				frame_times.erase(frame_times.begin());
			frame_times.push_back(frame_time);
		}

		poll_present_timing();
		poll_timestamps();

		auto cmd = device.request_command_buffer();
		auto rp = device.get_swapchain_render_pass(SwapchainRenderPass::ColorOnly);

		auto start_ts = cmd->write_timestamp(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
		cmd->begin_render_pass(rp);

		uint32_t burn_count_mul = burn_count * 100;
		cmd->push_constants(&burn_count_mul, 0, sizeof(burn_count_mul));
		CommandBufferUtil::draw_fullscreen_quad(*cmd, "builtin://shaders/quad.vert", "assets://shaders/burn.frag");
		VkClearValue clear_value = {};
		clear_value.color.float32[0] = 0.01f;
		clear_value.color.float32[1] = 0.02f;
		clear_value.color.float32[2] = 0.03f;
		cmd->clear_quad(0, {{{ 0, 0 }, { uint32_t(cmd->get_viewport().width), uint32_t(cmd->get_viewport().height) }}, 0, 1},
						clear_value);

		flat.begin();

		for (unsigned i = 0; i < count; i++)
		{
			flat.render_quad({ 0.0f, 0.0f, 4.0f }, { cmd->get_viewport().width, cmd->get_viewport().height },
			                 { 1.0f, 0.0f, 0.0f, 2.0f / 255.0f });
		}

		vec3 offset = { 10.0f, 10.0f, 0.0f };
		vec2 size = { cmd->get_viewport().width - 20.0f, cmd->get_viewport().height - 20.0f };

		const auto print_line = [&](const char *fmt, ...)
		{
			char text[1024];
			va_list va;
			va_start(va, fmt);
			vsnprintf(text, sizeof(text), fmt, va);
			va_end(va);

			flat.render_text(GRANITE_UI_MANAGER()->get_font(UI::FontSize::Normal),
			                 text, offset, size, vec4(1.0f, 1.0f, 0.0f, 1.0f),
			                 Font::Alignment::TopRight);

			offset.y += 30.0f;
		};

		auto minmax = minmax_range(frame_times.begin(), frame_times.end());
		auto avg = average_range(frame_times.begin(), frame_times.end());

		print_line("Average CPU sampled frame time: %.3f ms", 1000.0 * avg);
		print_line("Minimum CPU sampled frame time: %.3f ms", 1000.0 * minmax.first);
		print_line("Maximum CPU sampled frame time: %.3f ms", 1000.0 * minmax.second);
		print_line("Burn iterations: %u", burn_count_mul);
		if (!retired_results.empty())
		{
			auto avg_burn = average_range(retired_results.begin(), retired_results.end(),
			                              [](const QueryResult &result) { return result.burn_time; });
			print_line("Burn GPU time (Up/Down to toggle): %.3f ms", avg_burn * 1000.0);
		}
		print_line("%s", refresh_info.mode == RefreshMode::Unknown ? "FRR vs VRR unknown" :
		                                       refresh_info.mode == RefreshMode::VRR ? "VRR" : "FRR");
		print_line("Reported refreshDuration %.3f ms", refresh_info.refresh_duration * 1e-6);
		if (refresh_info.mode == RefreshMode::FRR)
			print_line("Reported refreshInterval %.3f ms", refresh_info.refresh_interval * 1e-6);
		print_line("Supports targetTime: %s", supports_request ? "yes" : "no");
		print_line("Force VRR relative timing (V to toggle): %s", force_vrr_timing ? "yes" : "no");
		print_line("Timing request (T to toggle): %s", timing_request ? "yes" : "no");
		print_line("PresentWait low latency (P to toggle): %s", present_wait_low_latency ? "yes" : "no");
		print_line("GPU submit low latency (L to toggle): %s", gpu_submit_low_latency ? "yes" : "no");
		print_line("Relative timing (R to toggle): %s", relative_timing ? "yes" : "no");

		if (refresh_info.refresh_duration)
		{
			print_line("Minimum target frame time: %u / %u cycles, %.3f ms (Left/Right to toggle)",
			           cycles_num, cycles_den, 1e-6 * double(refresh_info.refresh_duration * cycles_num / cycles_den));
		}

		offset = { 100.0f, 100.0f, 0.0f };
		size = { 600.0f, 150.0f };

		// Render CPU timestamps
		{
			if (refresh_info.refresh_duration)
				print_line("CPU sampled frame time range 0 - 4 refresh cycles");
			else
				print_line("CPU sampled frame time range %.3f ms - %.3f ms", 1000.0 * minmax.first, 1000.0 * minmax.second);

			flat.render_quad(offset, size, vec4(0.0f, 0.0f, 0.0f, 0.9f));
			render_history(frame_times.data(), frame_times.size(), offset.xy(), size);
			offset.y += size.y + 10.0f;
		}

		// GPU -> present delays
		{
			Util::SmallVector<double, WindowSize> gpu_done_present_delays;
			for (auto &retired : retired_results)
				if (retired.present_done && retired.queue_done)
					gpu_done_present_delays.push_back(1e-9 * double(int64_t(retired.present_done) - int64_t(retired.queue_done)));

			if (!gpu_done_present_delays.empty())
			{
				minmax = minmax_range(gpu_done_present_delays.begin(), gpu_done_present_delays.end());
				if (refresh_info.refresh_duration)
					print_line("GPU done to present complete delay (time range 0 - 4 refresh cycles)");
				else
					print_line("GPU done to present complete delay (time range %.3f ms - %.3f ms",
					           1000.0 * minmax.first, 1000.0 * minmax.second);

				flat.render_quad(offset, size, vec4(0.0f, 0.0f, 0.0f, 0.9f));
				render_history(gpu_done_present_delays.data(), gpu_done_present_delays.size(), offset.xy(), size);
				offset.y += size.y + 10.0f;
			}
		}

		// CPU record -> present delay
		{
			Util::SmallVector<double, WindowSize> cpu_record_present_delays;
			for (auto &retired : retired_results)
				if (retired.present_done && retired.queue_done)
					cpu_record_present_delays.push_back(1e-9 * double(int64_t(retired.present_done) - int64_t(retired.cpu_time_submit)));

			if (!cpu_record_present_delays.empty())
			{
				minmax = minmax_range(cpu_record_present_delays.begin(), cpu_record_present_delays.end());
				if (refresh_info.refresh_duration)
					print_line("CPU record to present complete delay (time range 0 - 4 refresh cycles)");
				else
					print_line("CPU record to present complete delay (time range %.3f ms - %.3f ms",
					           1000.0 * minmax.first, 1000.0 * minmax.second);

				flat.render_quad(offset, size, vec4(0.0f, 0.0f, 0.0f, 0.9f));
				render_history(cpu_record_present_delays.data(), cpu_record_present_delays.size(), offset.xy(), size);
				offset.y += size.y + 10.0f;
			}
		}

		// Error from estimated completion vs actual completion
		if (refresh_info.refresh_duration)
		{
			Util::SmallVector<double, WindowSize> errors;
			for (auto &retired : retired_results)
				errors.push_back(1e-9 * double(retired.error + 2 * int64_t(refresh_info.refresh_duration)));

			if (!errors.empty())
			{
				print_line("Presentation error (+/- 2 refresh cycles)");
				flat.render_quad(offset, size, vec4(0.0f, 0.0f, 0.0f, 0.9f));
				render_history(errors.data(), errors.size(), offset.xy(), size);
				offset.y += size.y + 10.0f;
			}
		}

		flat.flush(*cmd, vec3(0.0f), { cmd->get_viewport().width, cmd->get_viewport().height, 5.0f });

		cmd->end_render_pass();
		auto end_ts = cmd->write_timestamp(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

		PendingQueryResult pending = {};
		pending.start = std::move(start_ts);
		pending.end = std::move(end_ts);
		pending.present_id = wsi.get_last_submitted_present_id() + 1;
		pending.cpu_time_submit = Util::get_current_time_nsecs();
		queries.push_back(std::move(pending));

		device.submit(cmd);
	}

	unsigned counter = 0;
	Util::SmallVector<double, WindowSize> frame_times;
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
		auto *app = new PresentTiming(count);
		return app;
	}
	catch (const std::exception &e)
	{
		LOGE("application_create() threw exception: %s\n", e.what());
		return nullptr;
	}
}
}
