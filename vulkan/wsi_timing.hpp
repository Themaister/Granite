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
	void init(VkDevice device, VkSwapchainKHR swapchain, uint32_t swap_interval = 1);
	void begin_frame(double &frame_time, double &elapsed_time);

	bool fill_present_info_timing(VkPresentTimeGOOGLE &time);

private:
	VkDevice device = VK_NULL_HANDLE;
	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	uint32_t swap_interval = 0;

	enum { NUM_TIMINGS = 32, NUM_TIMING_MASK = NUM_TIMINGS - 1 };

	struct Serial
	{
		uint32_t serial = 0;
	} serial;

	struct Timing
	{
		uint32_t wall_serial = 0;
		uint64_t wall_frame_begin = 0;
		VkPastPresentationTimingGOOGLE timing = {};
	};

	struct Feedback
	{
		uint64_t refresh_interval = 0;
		Timing past_timings[NUM_TIMINGS];
		std::vector<VkPastPresentationTimingGOOGLE> timing_buffer;
	} feedback;

	struct Pacing
	{
		uint32_t base_serial = 0;
		uint64_t base_present = 0;
		bool have_estimate = false;
		bool have_real_estimate = false;
	} pacing;

	struct FrameTimer
	{
		uint64_t present_time = 0;
		uint64_t serial = 0;
	} last_frame;

	struct SmoothTimer
	{
		double elapsed = 0.0;
		double offset = 0.0;
	} smoothing;

	uint64_t compute_target_present_time_for_serial(uint32_t serial);
	uint64_t get_wall_time();
	void update_past_presentation_timing();
	const Timing *find_latest_timestamp(uint32_t start_serial) const;
	void update_frame_pacing(uint32_t id, uint64_t present_time, bool wall_time);
	void update_refresh_interval();
	void update_frame_time_smoothing(double &frame_time, double &elapsed_time);
};
}