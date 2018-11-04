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

#ifndef _WIN32
#include <time.h>
#endif

namespace Vulkan
{
void WSITiming::init(VkDevice device, VkSwapchainKHR swapchain)
{
	this->device = device;
	this->swapchain = swapchain;

	id = 0;
	pacing_base_id = 0;
	pacing_base_target = 0;
	have_pacing_estimate = false;
	have_real_pacing_estimate = false;

	last_time_id = 0;
	last_time_stamp = 0;

	swap_interval = 1;
	refresh_interval = 0;
	timing_buffer.resize(64);
	memset(past_timings, 0, sizeof(past_timings));
}

void WSITiming::update_refresh_interval()
{
	VkRefreshCycleDurationGOOGLE refresh;
	if (vkGetRefreshCycleDurationGOOGLE(device, swapchain, &refresh) == VK_SUCCESS)
	{
		if (!refresh_interval)
			LOGI("Observed refresh interval: %.6f Hz.\n", 1e9 / refresh.refreshDuration);
		refresh_interval = refresh.refreshDuration;
	}
	else
		LOGE("Failed to get refresh cycle duration.\n");
}

const WSITiming::Timing *WSITiming::find_latest_timestamp(uint32_t start_id) const
{
	for (uint32_t i = 1; i < NUM_TIMINGS - 1; i++)
	{
		uint32_t past_id = start_id - i;
		auto &past = past_timings[past_id & NUM_TIMING_MASK];
		if (past.wall_id == past_id && past.timing.actualPresentTime != 0)
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
		if (presentation_count > timing_buffer.size())
			timing_buffer.resize(presentation_count);
		auto res = vkGetPastPresentationTimingGOOGLE(device, swapchain, &presentation_count, timing_buffer.data());

		// I have a feeling this is racy in nature and we might have received another presentation timing in-between
		// querying count and getting actual data, so accept INCOMPLETE here.
		if (res == VK_SUCCESS || res == VK_INCOMPLETE)
		{
			for (uint32_t i = 0; i < presentation_count; i++)
			{
				auto &t = past_timings[timing_buffer[i].presentID & NUM_TIMING_MASK];
				if (t.wall_id == timing_buffer[i].presentID)
					t.timing = timing_buffer[i];

				update_frame_pacing(t.wall_id, t.timing.actualPresentTime, false);
			}
		}
	}

	auto *timing = find_latest_timestamp(id);
	if (timing)
	{
		if (timing->timing.actualPresentTime >= timing->wall_time)
		{
			auto latency = timing->timing.actualPresentTime - timing->wall_time;
			auto complete = timing->timing.earliestPresentTime - timing->timing.presentMargin;

			if (int64_t(timing->timing.presentMargin) < 0)
				LOGE("Present margin is negative (%lld) ... ?!\n", static_cast<long long>(timing->timing.presentMargin));

			if (timing->timing.earliestPresentTime > timing->timing.actualPresentTime)
				LOGE("Earliest present time is > actual present time ... Bug?\n");

			auto slack = int64_t(timing->timing.actualPresentTime - complete);
			LOGI("Latency: %.3f ms, slack: %.3f\n", latency * 1e-6, slack * 1e-6);
		}

		if (last_time_id && timing->wall_id != last_time_id)
		{
			LOGI("Frame time ID #%u: %.3f ms\n",
			     timing->wall_id,
			     1e-6 * double(timing->timing.actualPresentTime - last_time_stamp) / (timing->wall_id - last_time_id));
		}

		last_time_id = timing->wall_id;
		last_time_stamp = timing->timing.actualPresentTime;
	}
}

uint64_t WSITiming::get_wall_time()
{
#ifndef _WIN32
	// GOOGLE_display_timing on Linux and Android use CLOCK_MONOTONIC.
	timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000000000ull + ts.tv_nsec;
#else
	return 0;
#endif
}

void WSITiming::update_frame_pacing(uint32_t id, uint64_t present_time, bool wall_time)
{
	if (!have_pacing_estimate)
	{
		pacing_base_id = id;
		pacing_base_target = present_time;
		have_pacing_estimate = true;
		return;
	}

	if (!wall_time)
		have_real_pacing_estimate = true;

	if (wall_time && !have_real_pacing_estimate)
	{
		// We don't have a refresh interval yet, just update the estimate from CPU.
		pacing_base_id = id;
		pacing_base_target = present_time;
		return;
	}

	if (!refresh_interval)
	{
		// If we don't have a refresh interval, so we cannot estimate anything.
		// What we can do instead is just to blindly use the latest observed timestamp as our guiding hand.
		if (present_time > pacing_base_target)
		{
			pacing_base_id = id;
			pacing_base_target = present_time;
		}
	}
	else
	{
		int32_t frame_dist = int32_t(id - pacing_base_id);

		// Don't update with the past.
		if (frame_dist <= 0)
			return;

		// Extrapolate timing from current.
		uint64_t extrapolated_present_time =
				pacing_base_target + refresh_interval * swap_interval * (id - pacing_base_id);
		int64_t error = std::abs(int64_t(extrapolated_present_time - present_time));

		// If the delta is close enough (same frame),
		// update the base ID, so we can make more accurate future estimates.
		// This is relevant if we want to dynamically change swap interval.
		// If present time is significantly larger than extrapolated time,
		// we can assume we had a dropped frame, so we also need to update our base estimate.
		if ((present_time > extrapolated_present_time) || (error < int64_t(refresh_interval / 2)))
		{
			// We must have dropped frames, or similar.
			// Update our base estimate.
			pacing_base_id = id;
			pacing_base_target = present_time;
		}
	}
}

void WSITiming::begin_frame()
{
	id++;

	// On X11, this is found over time by observation, so we need to adapt it.
	// Only after we have observed the refresh cycle duration, we can start syncing against it.
	if ((id & 7) == 0)
		update_refresh_interval();

	auto &new_timing = past_timings[id & NUM_TIMING_MASK];
	new_timing.wall_id = id;
	new_timing.wall_time = get_wall_time();
	new_timing.timing = {};

	// Absolute minimum case, just get some initial data.
	update_frame_pacing(id, new_timing.wall_time, true);

	update_past_presentation_timing();

	// TODO: Here we could choose to hold the application back in case we have too much latency for our own good.
}

uint64_t WSITiming::compute_target_present_time_for_id(uint32_t id)
{
	if (!have_pacing_estimate)
		return 0;

	uint64_t frame_delta = id - pacing_base_id;
	frame_delta *= swap_interval;

	// Want to set the desired target close enough,
	// but not exactly at estimated target, since we have a rounding error cliff.
	uint64_t target = pacing_base_target + refresh_interval * (frame_delta - 1);
	target += (3 * refresh_interval) >> 2;

	return target;
}

bool WSITiming::fill_present_info_timing(VkPresentTimeGOOGLE &time)
{
	time.presentID = id;
	time.desiredPresentTime = compute_target_present_time_for_id(id);
	return true;
}

}
