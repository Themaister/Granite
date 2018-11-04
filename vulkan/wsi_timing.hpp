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

#pragma once

#include "vulkan.hpp"

namespace Vulkan
{
class WSITiming
{
public:
	void init(VkDevice device, VkSwapchainKHR swapchain);
	void begin_frame();

	bool fill_present_info_timing(VkPresentTimeGOOGLE &time);

private:
	VkDevice device = VK_NULL_HANDLE;
	VkSwapchainKHR swapchain = VK_NULL_HANDLE;

	struct Timing
	{
		uint32_t wall_id = 0;
		uint64_t wall_time = 0;
		VkPastPresentationTimingGOOGLE timing = {};
	};

	enum { NUM_TIMINGS = 32, NUM_TIMING_MASK = NUM_TIMINGS - 1 };

	uint64_t refresh_interval = 0;
	uint32_t id = 0;
	Timing past_timings[NUM_TIMINGS];
	std::vector<VkPastPresentationTimingGOOGLE> timing_buffer;

	uint32_t swap_interval = 0;

	uint64_t compute_target_present_time_for_id(uint32_t id);
	uint64_t get_wall_time();
	void update_past_presentation_timing();
	const Timing *find_latest_timestamp(uint32_t start_id) const;

	uint32_t pacing_base_id = 0;
	uint64_t pacing_base_target = 0;
	bool have_pacing_estimate = false;
	bool have_real_pacing_estimate = false;
	void update_frame_pacing(uint32_t id, uint64_t present_time, bool wall_time);
	void update_refresh_interval();

	uint64_t last_time_stamp = 0;
	uint64_t last_time_id = 0;
};
}