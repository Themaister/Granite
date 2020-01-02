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

#pragma once

#include "wsi.hpp"
#include "input.hpp"

namespace Granite
{
class GraniteWSIPlatform : public Vulkan::WSIPlatform
{
public:
	InputTracker &get_input_tracker()
	{
		return tracker;
	}

protected:
	void event_device_created(Vulkan::Device *device) override;
	void event_device_destroyed() override;
	void event_swapchain_created(Vulkan::Device *device, unsigned width, unsigned height,
	                             float aspect_ratio, size_t image_count, VkFormat format, VkSurfaceTransformFlagBitsKHR pre_rotate) override;
	void event_swapchain_destroyed() override;
	void event_swapchain_index(Vulkan::Device *device, unsigned index) override;
	void event_frame_tick(double frame, double elapsed) override;
	void event_display_timing_stutter(uint32_t current_serial, uint32_t observed_serial,
	                                  unsigned dropped_frames) override;

private:
	InputTracker tracker;
};
}
