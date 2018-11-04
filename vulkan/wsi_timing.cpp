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

#include "wsi_timing.hpp"
#include <string.h>
#include <algorithm>
#include <cmath>

#ifndef _WIN32
#include <time.h>
#endif

namespace Vulkan
{
void WSITiming::init(VkDevice device, VkSwapchainKHR swapchain, const WSITimingOptions &options)
{
	this->device = device;
	this->swapchain = swapchain;
	this->options = options;

	serial = {};
	pacing = {};
	last_frame = {};
	feedback = {};
	smoothing = {};
	feedback.timing_buffer.resize(64);
}

void WSITiming::update_refresh_interval()
{
	VkRefreshCycleDurationGOOGLE refresh;
	if (vkGetRefreshCycleDurationGOOGLE(device, swapchain, &refresh) == VK_SUCCESS)
	{
		if (!feedback.refresh_interval)
			LOGI("Observed refresh rate: %.6f Hz.\n", 1e9 / refresh.refreshDuration);
		feedback.refresh_interval = refresh.refreshDuration;
	}
	else
		LOGE("Failed to get refresh cycle duration.\n");
}

const WSITiming::Timing *WSITiming::find_latest_timestamp(uint32_t start_serial) const
{
	for (uint32_t i = 1; i < NUM_TIMINGS - 1; i++)
	{
		uint32_t past_serial = start_serial - i;
		auto &past = feedback.past_timings[past_serial & NUM_TIMING_MASK];
		if (past.wall_serial == past_serial && past.timing.actualPresentTime != 0)
			return &past;
	}

	return nullptr;
}

void WSITiming::update_past_presentation_timing()
{
	// Update past presentation timings.
	uint32_t presentation_count;
	if (vkGetPastPresentationTimingGOOGLE(device, swapchain, &presentation_count, nullptr) != VK_SUCCESS)
		return;

	if (presentation_count)
	{
		if (presentation_count > feedback.timing_buffer.size())
			feedback.timing_buffer.resize(presentation_count);
		auto res = vkGetPastPresentationTimingGOOGLE(device, swapchain, &presentation_count, feedback.timing_buffer.data());

		// I have a feeling this is racy in nature and we might have received another presentation timing in-between
		// querying count and getting actual data, so accept INCOMPLETE here.
		if (res == VK_SUCCESS || res == VK_INCOMPLETE)
		{
			for (uint32_t i = 0; i < presentation_count; i++)
			{
				auto &t = feedback.past_timings[feedback.timing_buffer[i].presentID & NUM_TIMING_MASK];
				if (t.wall_serial == feedback.timing_buffer[i].presentID)
				{
					t.timing = feedback.timing_buffer[i];

					uint64_t gpu_done_time = (t.timing.earliestPresentTime - t.timing.presentMargin);
					t.slack = int64_t(t.timing.actualPresentTime - gpu_done_time);
					t.pipeline_latency = int64_t(gpu_done_time - t.wall_frame_begin);
				}

				update_frame_pacing(t.wall_serial, t.timing.actualPresentTime, false);
			}
		}
	}

	auto *timing = find_latest_timestamp(serial.serial);
	if (timing && timing->timing.actualPresentTime >= timing->wall_frame_begin)
	{
		auto total_latency = timing->timing.actualPresentTime - timing->wall_frame_begin;
		feedback.latency = 0.99 * feedback.latency + 0.01 * total_latency;

		if (int64_t(timing->timing.presentMargin) < 0)
			LOGE("Present margin is negative (%lld) ... ?!\n", static_cast<long long>(timing->timing.presentMargin));

		if (timing->timing.earliestPresentTime > timing->timing.actualPresentTime)
			LOGE("Earliest present time is > actual present time ... Bug?\n");

		// How much can we squeeze latency?
		auto slack = timing->slack;
		if (options.debug)
			LOGI("Total latency: %.3f ms, slack time: %.3f\n", total_latency * 1e-6, slack * 1e-6);

		if (last_frame.serial && timing->wall_serial != last_frame.serial)
		{
			if (options.debug)
			{
				LOGI("Frame time ID #%u: %.3f ms\n",
				     timing->wall_serial,
				     1e-6 * double(timing->timing.actualPresentTime - last_frame.present_time) /
				     double(timing->wall_serial - last_frame.serial));
			}
		}

		last_frame.serial = timing->wall_serial;
		last_frame.present_time = timing->timing.actualPresentTime;
	}
}

void WSITiming::wait_until(int64_t nsecs)
{
#ifndef _WIN32
	timespec ts;
	ts.tv_sec = nsecs / 1000000000;
	ts.tv_nsec = nsecs % 1000000000;
	clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr);
#else
	(void)nsecs;
#endif
}

uint64_t WSITiming::get_wall_time()
{
#ifndef _WIN32
	// GOOGLE_display_timing on Linux and Android use CLOCK_MONOTONIC explicitly.
	timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000000000ull + ts.tv_nsec;
#else
	return 0;
#endif
}

void WSITiming::update_frame_pacing(uint32_t serial, uint64_t present_time, bool wall_time)
{
	if (!pacing.have_estimate)
	{
		pacing.base_serial = serial;
		pacing.base_present = present_time;
		pacing.have_estimate = true;
		return;
	}

	if (!wall_time)
		pacing.have_real_estimate = true;

	if (wall_time && !pacing.have_real_estimate)
	{
		// We don't have a refresh interval yet, just update the estimate from CPU.
		pacing.base_serial = serial;
		pacing.base_present = present_time;
		return;
	}

	if (!feedback.refresh_interval)
	{
		// If we don't have a refresh interval yet, we cannot estimate anything.
		// What we can do instead is just to blindly use the latest observed timestamp as our guiding hand.
		if (present_time > pacing.base_present)
		{
			pacing.base_serial = serial;
			pacing.base_present = present_time;
		}
	}
	else
	{
		int32_t frame_dist = int32_t(serial - pacing.base_serial);

		// Don't update with the past.
		if (frame_dist <= 0)
			return;

		// Extrapolate timing from current.
		uint64_t extrapolated_present_time =
				pacing.base_present + feedback.refresh_interval * options.swap_interval * (serial - pacing.base_serial);
		int64_t error = std::abs(int64_t(extrapolated_present_time - present_time));

		// If the delta is close enough (expected frame pace),
		// update the base ID, so we can make more accurate future estimates.
		// This is relevant if we want to dynamically change swap interval.
		// If present time is significantly larger than extrapolated time,
		// we can assume we had a dropped frame, so we also need to update our base estimate.
		if ((present_time > extrapolated_present_time) || (error < int64_t(feedback.refresh_interval / 2)))
		{
			// We must have dropped frames, or similar.
			// Update our base estimate.
			pacing.base_serial = serial;
			pacing.base_present = present_time;
		}
	}
}

void WSITiming::update_frame_time_smoothing(double &frame_time, double &elapsed_time)
{
	double target_frame_time = frame_time;
	if (feedback.refresh_interval)
		target_frame_time = double(options.swap_interval * feedback.refresh_interval) * 1e-9;

	double actual_elapsed = elapsed_time - smoothing.offset;
	smoothing.elapsed += target_frame_time;

	double delta = actual_elapsed - smoothing.elapsed;
	if (delta > std::fabs(target_frame_time * 4.0)) // We're way off, something must have happened, reset the smoothing.
	{
		if (options.debug)
			LOGI("Detected discontinuity in smoothing algorithm!\n");
		smoothing.offset = elapsed_time;
		smoothing.elapsed = 0.0;
		return;
	}

	double jitter_offset = 0.0;

	// Accept up to 0.5% jitter to catch up or slow down smoothly to our target elapsed time.
	if (delta > 0.1 * target_frame_time)
		jitter_offset = 0.005 * target_frame_time;
	else if (delta < -0.1 * target_frame_time)
		jitter_offset = -0.005 * target_frame_time;

	target_frame_time += jitter_offset;
	smoothing.elapsed += jitter_offset;

	elapsed_time = smoothing.elapsed + smoothing.offset;
	frame_time = target_frame_time;
}

double WSITiming::get_current_latency() const
{
	return feedback.latency;
}

void WSITiming::begin_frame(double &frame_time, double &elapsed_time)
{
	// Update initial frame elapsed estimate,
	// from here, we'll try to lock the frame time to refresh_rate +/- epsilon.
	if (serial.serial == 0)
	{
		smoothing.offset = elapsed_time;
		smoothing.elapsed = 0.0;
	}
	serial.serial++;

	// On X11, this is found over time by observation, so we need to adapt it.
	// Only after we have observed the refresh cycle duration, we can start syncing against it.
	if ((serial.serial & 7) == 0)
		update_refresh_interval();

	auto &new_timing = feedback.past_timings[serial.serial & NUM_TIMING_MASK];
	new_timing.wall_serial = serial.serial;
	new_timing.wall_frame_begin = get_wall_time();
	new_timing.timing = {};

	// Absolute minimum case, just get some initial data.
	update_frame_pacing(serial.serial, new_timing.wall_frame_begin, true);
	update_past_presentation_timing();
	update_frame_time_smoothing(frame_time, elapsed_time);

	if (options.latency_limiter != LatencyLimiter::None)
	{
		// Try to squeeze timings by sleeping, quite shaky, but very fun :)
		if (feedback.refresh_interval)
		{
			int64_t target = int64_t(compute_target_present_time_for_serial(serial.serial));

			if (options.latency_limiter == LatencyLimiter::AdaptiveLowLatency)
			{
				int64_t latency = 0;
				if (get_conservative_latency(latency))
				{
					// Keep quarter frame as buffer in case this frame is heavier than normal.
					latency += feedback.refresh_interval >> 2;
					wait_until(target - latency);

					uint64_t old_time = new_timing.wall_frame_begin;
					new_timing.wall_frame_begin = get_wall_time();
					if (options.debug)
					{
						LOGI("Slept for %.3f ms for latency tuning.\n",
						     1e-6 * (new_timing.wall_frame_begin - old_time));
					}
				}
			}
			else if (options.latency_limiter == LatencyLimiter::IdealPipeline)
			{
				// In the ideal pipeline we have one frame for CPU to work,
				// then one frame for GPU to work in parallel, so we should strive for ~1.5 frames of latency here.
				// The assumption is that we can kick some work to GPU at least mid-way through our frame.
				int64_t latency = (feedback.refresh_interval * 3) >> 1;
				wait_until(target - latency);

				uint64_t old_time = new_timing.wall_frame_begin;
				new_timing.wall_frame_begin = get_wall_time();
				if (options.debug)
				{
					LOGI("Slept for %.3f ms for latency tuning.\n",
					     1e-6 * (new_timing.wall_frame_begin - old_time));
				}
			}
		}
	}
}

bool WSITiming::get_conservative_latency(int64_t &latency) const
{
	latency = 0;
	unsigned valid_latencies = 0;
	for (auto &timing : feedback.past_timings)
	{
		if (timing.timing.actualPresentTime >= timing.wall_frame_begin)
		{
			latency = std::max(latency, timing.pipeline_latency);
			valid_latencies++;
		}
	}

	return valid_latencies > (NUM_TIMINGS / 2);
}

uint64_t WSITiming::compute_target_present_time_for_serial(uint32_t serial)
{
	if (!pacing.have_estimate)
		return 0;

	uint64_t frame_delta = serial - pacing.base_serial;
	frame_delta *= options.swap_interval;

	// Want to set the desired target close enough,
	// but not exactly at estimated target, since we have a rounding error cliff.
	uint64_t target = pacing.base_present + feedback.refresh_interval * frame_delta;
	target -= feedback.refresh_interval >> 3;

	return target;
}

bool WSITiming::fill_present_info_timing(VkPresentTimeGOOGLE &time)
{
	time.presentID = serial.serial;
	time.desiredPresentTime = compute_target_present_time_for_serial(serial.serial);
	return true;
}

}
