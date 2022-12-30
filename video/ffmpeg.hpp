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
	// Should be called after stop().
	// If stop() is not called, this call with also do so.
	void end_device_context();

	// Starts decoding thread.
	bool play();
	// Stops decoding thread.
	bool stop();

	// Somewhat heavy blocking operation.
	// Needs to drain all decoding work, flush codecs and seek the AV file.
	// All image references are invalidated.
	bool seek(double ts);

	void set_paused(bool paused);
	bool get_paused() const;

	// Audio is played back with a certain amount of latency.
	// Audio is played asynchronously if a mixer is provided and the stream has an audio track.
	// A worker thread will ensure that the audio mixer can render audio on-demand.
	// If audio stream does not exist, returns negative number.
	// Application should fall back to other means of timing in this scenario.
	double get_estimated_audio_playback_timestamp();

	// Client is responsible for displaying the frame in due time.
	// A video frame can be released when the returned PTS is out of date.
	bool acquire_video_frame(VideoFrame &frame);

	// Poll acquire. Returns positive on success, 0 on no available image, negative number on EOF.
	int try_acquire_video_frame(VideoFrame &frame);

	void release_video_frame(unsigned index, Vulkan::Semaphore sem);

private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};
}
