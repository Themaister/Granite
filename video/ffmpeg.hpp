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

#include "device.hpp"
#include "image.hpp"

namespace Granite
{
namespace Audio
{
class DumpBackend;
}

class VideoEncoder
{
public:
	VideoEncoder();
	~VideoEncoder();

	struct Timebase
	{
		int num;
		int den;
	};

	struct Options
	{
		unsigned width;
		unsigned height;
		Timebase frame_timebase;
	};

	void set_audio_source(Audio::DumpBackend *backend);

	bool init(Vulkan::Device *device, const char *path, const Options &options);
	bool push_frame(const Vulkan::Image &image, VkImageLayout layout,
	                Vulkan::CommandBuffer::Type type, const Vulkan::Semaphore &semaphore,
					Vulkan::Semaphore &release_semaphore);
	void drain();

private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};
}
