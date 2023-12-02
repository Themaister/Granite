#pragma once

#include "device.hpp"
#include "image.hpp"
#include "semaphore.hpp"
#include "slangmosh_decode_iface.hpp"
#include "pyro_protocol.h"

namespace Granite
{
namespace Audio
{
class Mixer;
struct StreamID;
}

struct VideoFrame
{
	const Vulkan::ImageView *view = nullptr;
	Vulkan::Semaphore sem;
	unsigned index = 0;
	double pts = 0.0;
	double done_ts = 0.0;
};

class DemuxerIOInterface
{
public:
	virtual ~DemuxerIOInterface() = default;
	virtual pyro_codec_parameters get_codec_parameters() = 0;
	virtual bool wait_next_packet() = 0;
	virtual const void *get_data() = 0;
	virtual size_t get_size() = 0;
	virtual pyro_payload_header get_payload_header() = 0;
};

class VideoDecoder
{
public:
	VideoDecoder();
	~VideoDecoder();

	struct DecodeOptions
	{
		bool mipgen = false;
		bool realtime = false;
		bool blocking = false;
		float target_video_buffer_time = 0.2f;
		float target_realtime_audio_buffer_time = 0.5f;
		const char *hwdevice = nullptr;
	};

	void set_io_interface(DemuxerIOInterface *iface);

	bool init(Audio::Mixer *mixer, const char *path, const DecodeOptions &options);
	unsigned get_width() const;
	unsigned get_height() const;

	// Must be called before play().
	bool begin_device_context(Vulkan::Device *device, const FFmpegDecode::Shaders<> &shaders);

	// Should be called after stop().
	// If stop() is not called, this call with also do so.
	void end_device_context();

	// Starts decoding thread and audio stream.
	bool play();

	// Can be called after play().
	// When seeking or stopping the stream, the ID may change spuriously and must be re-queried.
	// It's best to just query the ID for every operation.
	bool get_stream_id(Audio::StreamID &id) const;

	// Stops decoding thread.
	bool stop();

	// Somewhat heavy blocking operation.
	// Needs to drain all decoding work, flush codecs and seek the AV file.
	// All image references are invalidated.
	bool seek(double ts);

	void set_paused(bool paused);

	bool get_paused() const;

	// Sync strategies, do not mix and match!
	// Sync strategy #1 (non-realtime) - Optimal smoothness, high latency.
	// Audio is played back with a certain amount of latency.
	// Audio is played asynchronously if a mixer is provided and the stream has an audio track.
	// A worker thread will ensure that the audio mixer can render audio on-demand.
	// If audio stream does not exist, returns negative number.
	// Application should fall back to other means of timing in this scenario.
	double get_estimated_audio_playback_timestamp(double elapsed_time);

	// Sync strategy #2 (realtime) - Prioritize latency, bad pacing.
	// Should be called after every acquire in realtime mode.
	// Lets audio buffer speed up or slow down appropriately to try to match video.
	void latch_audio_buffering_target(double buffer_time);

	// Sync strategy #3 (realtime) - Balanced. Try to lock to a fixed latency while retaining smoothness.
	double latch_estimated_video_playback_timestamp(double elapsed_time, double target_latency);

	// Only based on audio PTS.
	double get_estimated_audio_playback_timestamp_raw();

	// Reports difference between last buffered PTS and current PTS.
	// Useful to figure out how much latency there is.
	double get_audio_buffering_duration();

	// Reports PTS of the last decoded frame.
	double get_last_video_buffering_pts();

	unsigned get_num_ready_video_frames();

	// Client is responsible for displaying the frame in due time.
	// A video frame can be released when the returned PTS is out of date.
	bool acquire_video_frame(VideoFrame &frame, int timeout_ms = -1);

	// Poll acquire. Returns positive on success, 0 on no available image, negative number on EOF.
	int try_acquire_video_frame(VideoFrame &frame);

	bool is_eof();

	void release_video_frame(unsigned index, Vulkan::Semaphore sem);

	float get_audio_sample_rate() const;

	uint32_t get_audio_underflow_counter() const;

private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};
}