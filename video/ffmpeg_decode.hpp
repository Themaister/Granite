#pragma once

#include "device.hpp"
#include "image.hpp"
#include "semaphore.hpp"

namespace Granite
{
namespace Audio
{
class Mixer;
}

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

	bool init(Audio::Mixer *mixer, const char *path);

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
	double get_estimated_audio_playback_timestamp(double elapsed_time);

	// Only based on audio PTS.
	double get_estimated_audio_playback_timestamp_raw();

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