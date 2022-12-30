/* Copyright (c) 2017-2022 Hans-Kristian Arntzen
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
class Mixer;
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

struct VideoFrame
{
	const Vulkan::ImageView *view = nullptr;
	Vulkan::Semaphore sem;
	unsigned index = 0;
	double pts = 0.0;
};

class VideoDecoder
{
public:
	VideoDecoder();
	~VideoDecoder();

	bool init(Granite::Audio::Mixer *mixer, const char *path);

	// Must be called before play().
	bool begin_device_context(Vulkan::Device *device);
	// Must be called after stop().
	void end_device_context();

	bool play();
	bool stop();
	bool rewind();

	// Audio is played back with a certain amount of latency.
	// Audio is played asynchronously if a mixer is provided and the stream has an audio track.
	// A worker thread will ensure that the audio mixer can render audio on-demand.
	double get_estimated_audio_playback_timestamp();

	// Next acquire will aim to grab an image with PTS at least equal to target timestamp,
	// and a PTS that is at least as large as one that has been previously acquired.
	// Client is responsible for displaying the frame in due time.
	bool acquire_video_frame(VideoFrame &frame);
	void release_video_frame(unsigned index, Vulkan::Semaphore sem);

private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};
}
