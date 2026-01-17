/* Copyright (c) 2017-2026 Hans-Kristian Arntzen
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

#include "command_buffer.hpp"
#include "image.hpp"
#include "buffer.hpp"

namespace Granite
{
class VideoScaler
{
public:
	void set_program(Vulkan::Program *scale);
	void reset();

	struct RescaleInfo
	{
		const Vulkan::ImageView *output_planes[3];
		unsigned num_output_planes;
		const Vulkan::ImageView *input;

		VkColorSpaceKHR input_color_space;
		VkColorSpaceKHR output_color_space;
	};

	void rescale(Vulkan::CommandBuffer &cmd, const RescaleInfo &info);

private:
	Vulkan::Program *scale = nullptr;
	Vulkan::BufferHandle weights;
	uint32_t last_input_width = 0, last_input_height = 0;
	uint32_t last_output_width = 0, last_output_height = 0;

	void update_weights(Vulkan::CommandBuffer &cmd, const RescaleInfo &info);
};
}