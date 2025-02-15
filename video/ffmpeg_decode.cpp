/* Copyright (c) 2017-2024 Hans-Kristian Arntzen
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

#define __STDC_LIMIT_MACROS 1

#include "ffmpeg_decode.hpp"
#include "ffmpeg_hw_device.hpp"
#include "logging.hpp"
#include "math.hpp"
#include "muglm/muglm_impl.hpp"
#include "muglm/matrix_helper.hpp"
#include "transforms.hpp"
#include "thread_group.hpp"
#include "global_managers.hpp"
#include "thread_priority.hpp"
#include "timeline_trace_file.hpp"
#include "timer.hpp"
#include "thread_name.hpp"
#include <condition_variable>
#include <mutex>
#include <thread>
#include <chrono>
#ifdef HAVE_GRANITE_AUDIO
#include "audio_mixer.hpp"
#include "dsp/dsp.hpp"
#include "dsp/sinc_resampler.hpp"
#endif

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#ifdef HAVE_FFMPEG_VULKAN
#include <libavutil/hwcontext_vulkan.h>
#endif
}

#ifndef AV_CHANNEL_LAYOUT_STEREO
// Legacy API.
#define AVChannelLayout uint64_t
#define ch_layout channel_layout
#define AV_CHANNEL_LAYOUT_STEREO AV_CH_LAYOUT_STEREO
#define AV_CHANNEL_LAYOUT_MONO AV_CH_LAYOUT_MONO
#define av_channel_layout_compare(pa, pb) ((*pa) != (*pb))
#endif

namespace Granite
{
struct CodecStream
{
	AVStream *av_stream = nullptr;
	AVCodecContext *av_ctx = nullptr;
	const AVCodec *av_codec = nullptr;
};

static void free_av_objects(CodecStream &stream)
{
	if (stream.av_ctx)
		avcodec_free_context(&stream.av_ctx);
}

#ifdef HAVE_GRANITE_AUDIO
struct AVFrameRingStream final : Audio::MixerStream, Util::ThreadSafeIntrusivePtrEnabled<AVFrameRingStream>
{
	AVFrameRingStream(float sample_rate, unsigned num_channels, double timebase, bool support_resample, bool blocking_mix);
	~AVFrameRingStream() override;

	float sample_rate;
	float out_sample_rate;
	float resampling_ratio = 1.0f;
	unsigned num_channels;
	double timebase;
	double inv_sample_rate_ns;
	bool blocking_mix;

	void set_rate_factor(float factor);
	float get_rate_factor() const noexcept;

	uint32_t get_underflow_counter() const;

	bool setup(float mixer_output_rate, unsigned mixer_channels, size_t max_num_frames) override;
	size_t accumulate_samples(float * const *channels, const float *gain, size_t num_frames) noexcept override;
	size_t accumulate_samples_inner(float * const *channels, const float *gain, size_t num_frames) noexcept;
	unsigned get_num_channels() const override;
	float get_sample_rate() const override;
	void dispose() override;

	uint32_t get_current_write_count();

	// Buffering in terms of AVFrame is a little questionable since packet sizes can vary a fair bit,
	// might have to revisit later.
	// In practice, any codec will have a reasonably short packet window (10ms - 20ms),
	// but not too long either.
	enum { Frames = 64, FramesHighWatermark = 48 };
	AVFrame *frames[Frames] = {};
	std::atomic_uint32_t write_count;
	std::atomic_uint32_t read_count;
	std::atomic_uint32_t read_frames_count;
	std::atomic_uint32_t write_frames_count;
	std::atomic_uint32_t rate_factor_u32;
	std::atomic_uint32_t underflows;
	std::atomic_bool complete;
	int packet_frames = 0;
	bool running_state = false;
	unsigned get_num_buffered_audio_frames();
	unsigned get_num_buffered_av_frames();

	enum { MaxChannels = 8 };
	std::unique_ptr<Audio::DSP::SincResampler> resamplers[MaxChannels];
	std::vector<float> tmp_resampler_buffer[MaxChannels];
	float *tmp_resampler_ptrs[MaxChannels] = {};

	struct
	{
		double pts = -1.0;
		int64_t sampled_ns = 0;
	} progress[Frames];
	std::atomic_uint32_t pts_index;

	AVFrame *acquire_write_frame();
	void mark_uncorked_audio_pts();
	void submit_write_frame();
	void mark_complete();

	std::condition_variable cond;
	std::mutex lock;
};

AVFrameRingStream::AVFrameRingStream(float sample_rate_, unsigned num_channels_, double timebase_,
                                     bool support_resample, bool blocking_mix_)
	: sample_rate(sample_rate_)
	, num_channels(num_channels_)
	, timebase(timebase_)
	, inv_sample_rate_ns(1e9 / sample_rate)
	, blocking_mix(blocking_mix_)
{
	for (auto &f : frames)
		f = av_frame_alloc();
	write_count = 0;
	read_count = 0;
	read_frames_count = 0;
	write_frames_count = 0;
	pts_index = 0;
	underflows = 0;
	complete = false;
	set_rate_factor(1.0f);

	if (support_resample)
		for (unsigned i = 0; i < num_channels; i++)
			resamplers[i] = std::make_unique<Audio::DSP::SincResampler>(sample_rate, sample_rate, Audio::DSP::SincResampler::Quality::High);
}

void AVFrameRingStream::set_rate_factor(float factor)
{
	factor = resampling_ratio / factor;
	uint32_t v;
	memcpy(&v, &factor, sizeof(uint32_t));
	rate_factor_u32.store(v, std::memory_order_relaxed);
}

float AVFrameRingStream::get_rate_factor() const noexcept
{
	float v;
	uint32_t u = rate_factor_u32.load(std::memory_order_relaxed);
	memcpy(&v, &u, sizeof(uint32_t));
	return v;
}

uint32_t AVFrameRingStream::get_underflow_counter() const
{
	return underflows.load(std::memory_order_relaxed);
}

void AVFrameRingStream::mark_uncorked_audio_pts()
{
	uint32_t index = (pts_index.load(std::memory_order_acquire) - 1) % Frames;

	// This is not a hazard, we know the mixer thread is done writing here.
	if (progress[index].pts >= 0.0)
		progress[index].sampled_ns = Util::get_current_time_nsecs();
}

bool AVFrameRingStream::setup(float mixer_output_rate, unsigned mixer_channels, size_t num_frames)
{
	// TODO: Could promote mono to stereo.
	if (mixer_channels != num_channels)
		return false;

	out_sample_rate = sample_rate;

	for (unsigned i = 0; i < MaxChannels; i++)
	{
		if (resamplers[i])
		{
			tmp_resampler_buffer[i].resize(num_frames * 2); // Maximum ratio distortion is 1.5x.
			tmp_resampler_ptrs[i] = tmp_resampler_buffer[i].data();

			// If we're resampling anyway, target native mixer rate.
			out_sample_rate = mixer_output_rate;
			resampling_ratio = out_sample_rate / sample_rate;
		}
	}

	return true;
}

void AVFrameRingStream::dispose()
{
	release_reference();
}

float AVFrameRingStream::get_sample_rate() const
{
	return out_sample_rate;
}

unsigned AVFrameRingStream::get_num_channels() const
{
	return num_channels;
}

size_t AVFrameRingStream::accumulate_samples(float *const *channels, const float *gain, size_t num_frames) noexcept
{
	if (resamplers[0])
	{
		float ratio = get_rate_factor();

		for (unsigned i = 0; i < num_channels; i++)
			resamplers[i]->set_sample_rate_ratio(ratio);
		size_t required = resamplers[0]->get_current_input_for_output_frames(num_frames);
		for (unsigned i = 0; i < num_channels; i++)
		{
			assert(required <= tmp_resampler_buffer[i].size());
			// Should have a no-accumulation variant, but eeeeeeh.
			// We need to clear out to zero anyway for underruns, etc.
			memset(tmp_resampler_ptrs[i], 0, required * sizeof(float));
		}
		size_t accum = accumulate_samples_inner(tmp_resampler_ptrs, gain, required);

		if (accum < required)
			underflows.store(underflows.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);

		for (unsigned i = 0; i < num_channels; i++)
		{
			resamplers[i]->set_sample_rate_ratio(ratio);
			resamplers[i]->process_and_accumulate_output_frames(channels[i], tmp_resampler_ptrs[i], num_frames);
		}

		return complete.load(std::memory_order_relaxed) && accum == 0 ? 0 : num_frames;
	}
	else
	{
		return accumulate_samples_inner(channels, gain, num_frames);
	}
}

uint32_t AVFrameRingStream::get_current_write_count()
{
	if (blocking_mix)
	{
		uint32_t r_count = read_count.load(std::memory_order_relaxed);
		uint32_t w_count = write_count.load(std::memory_order_acquire);
		if (r_count != w_count)
			return w_count;

		// Stall. This will block the mixer, so this should only be used when this audio stream is exclusive,
		// e.g. a standalone video player.
		// We never expect to wait more than a few milliseconds here, otherwise the audio buffer is drained already.
		std::unique_lock<std::mutex> holder{lock};
		cond.wait_for(holder, std::chrono::milliseconds(50), [this, r_count]() {
			return complete.load(std::memory_order_relaxed) ||
			       write_count.load(std::memory_order_relaxed) != r_count;
		});
		return write_count.load(std::memory_order_relaxed);
	}
	else
		return write_count.load(std::memory_order_acquire);
}

size_t AVFrameRingStream::accumulate_samples_inner(float *const *channels, const float *gain, size_t num_frames) noexcept
{
	// Hold back playback until we have buffered enough to avoid instant underrun.
	uint32_t written_count = write_count.load(std::memory_order_acquire);
	if (!running_state)
	{
		int buffered_audio_frames = 0;
		for (uint32_t i = 0; i < written_count; i++)
			buffered_audio_frames += frames[i]->nb_samples;

		// Wait until we have 50ms worth of audio buffered to avoid a potential instant underrun.
		if (float(buffered_audio_frames) <= sample_rate * 0.05f)
			return complete.load(std::memory_order_relaxed) ? 0 : num_frames;
		running_state = true;
	}

	size_t write_offset = 0;
	uint32_t buffer_index = read_count.load(std::memory_order_relaxed);

	while (write_offset < num_frames && buffer_index != (written_count = get_current_write_count()))
	{
		size_t to_write = num_frames - write_offset;
		auto *frame = frames[buffer_index % Frames];
		if (packet_frames < frame->nb_samples)
		{
			to_write = std::min<size_t>(frame->nb_samples - packet_frames, to_write);

			// Update latest audio PTS.
			// TODO: Might have to also mark when this PTS was written,
			// along with some way to compensate for latency.
			// However, the audio backend latency is fairly low and is comparable with
			// video latency, so we might be able to get away with simply ignoring it.
			if (packet_frames == 0)
			{
				uint32_t pts_buffer_index = pts_index.load(std::memory_order_relaxed);
				auto new_pts = double(frame->pts) * timebase;
				auto &p = progress[pts_buffer_index % Frames];
				p.pts = new_pts;
				p.sampled_ns = Util::get_current_time_nsecs();
				// If we're deep into mixing, we need to compensate for the fact that this PTS will be delayed
				// a little when played back.
				p.sampled_ns += int64_t(double(write_offset) * inv_sample_rate_ns);
				pts_index.store(pts_buffer_index + 1, std::memory_order_release);
			}

			if (frame->format == AV_SAMPLE_FMT_FLTP || (frame->format == AV_SAMPLE_FMT_FLT && num_channels == 1))
			{
				for (unsigned i = 0; i < num_channels; i++)
				{
					Audio::DSP::accumulate_channel(
							channels[i] + write_offset,
							reinterpret_cast<const float *>(frame->data[i]) + packet_frames,
							gain[i], to_write);
				}
			}
			else if (frame->format == AV_SAMPLE_FMT_FLT)
			{
				// We only care about supporting STEREO here.
				Audio::DSP::accumulate_channel_deinterleave_stereo(
						channels[0] + write_offset, channels[1] + write_offset,
						reinterpret_cast<const float *>(frame->data[0]) + 2 * packet_frames,
						gain, to_write);
			}
			else if (frame->format == AV_SAMPLE_FMT_S32P || (frame->format == AV_SAMPLE_FMT_S32 && num_channels == 1))
			{
				for (unsigned i = 0; i < num_channels; i++)
				{
					Audio::DSP::accumulate_channel_s32(
							channels[i] + write_offset,
							reinterpret_cast<const int32_t *>(frame->data[i]) + packet_frames,
							gain[i], to_write);
				}
			}
			else if (frame->format == AV_SAMPLE_FMT_S32)
			{
				Audio::DSP::accumulate_channel_deinterleave_stereo_s32(
						channels[0] + write_offset, channels[1] + write_offset,
						reinterpret_cast<const int32_t *>(frame->data[0]) + 2 * packet_frames,
						gain, to_write);
			}
			else if (frame->format == AV_SAMPLE_FMT_S16P || (frame->format == AV_SAMPLE_FMT_S16 && num_channels == 1))
			{
				for (unsigned i = 0; i < num_channels; i++)
				{
					Audio::DSP::accumulate_channel_s16(
							channels[i] + write_offset,
							reinterpret_cast<const int16_t *>(frame->data[i]) + packet_frames,
							gain[i], to_write);
				}
			}
			else if (frame->format == AV_SAMPLE_FMT_S16)
			{
				Audio::DSP::accumulate_channel_deinterleave_stereo_s16(
						channels[0] + write_offset, channels[1] + write_offset,
						reinterpret_cast<const int16_t *>(frame->data[0]) + 2 * packet_frames,
						gain, to_write);
			}

			packet_frames += int(to_write);
			write_offset += to_write;
		}
		else
		{
			// We've consumed this packet, retire it.
			packet_frames = 0;
			buffer_index++;
		}
	}

	read_count.store(buffer_index, std::memory_order_release);
	read_frames_count.store(read_frames_count.load(std::memory_order_relaxed) + uint32_t(write_offset),
	                        std::memory_order_release);

	return complete.load(std::memory_order_relaxed) ? write_offset : num_frames;
}

AVFrame *AVFrameRingStream::acquire_write_frame()
{
	auto index = write_count.load(std::memory_order_relaxed) % Frames;
	auto *frame = frames[index];
	return frame;
}

void AVFrameRingStream::submit_write_frame()
{
	if (blocking_mix)
		lock.lock();

	uint32_t index = write_count.load(std::memory_order_relaxed);
	write_frames_count.store(write_frames_count.load(std::memory_order_relaxed) + uint32_t(frames[index % Frames]->nb_samples));
	write_count.store(index + 1, std::memory_order_release);

	if (blocking_mix)
	{
		cond.notify_one();
		lock.unlock();
	}
}

void AVFrameRingStream::mark_complete()
{
	if (blocking_mix)
	{
		std::lock_guard<std::mutex> holder{lock};
		complete.store(true, std::memory_order_relaxed);
		cond.notify_one();
	}
	else
		complete.store(true, std::memory_order_relaxed);
}

unsigned AVFrameRingStream::get_num_buffered_av_frames()
{
	uint32_t read_index = read_count.load(std::memory_order_acquire);
	uint32_t count = write_count - read_index;
	return count;
}

unsigned AVFrameRingStream::get_num_buffered_audio_frames()
{
	uint32_t result = write_frames_count.load(std::memory_order_relaxed) - read_frames_count.load(std::memory_order_acquire);
	VK_ASSERT(result < 0x80000000u);
	return result;
}

AVFrameRingStream::~AVFrameRingStream()
{
	for (auto *f : frames)
		av_frame_free(&f);
}
#endif

struct VideoDecoder::Impl
{
	Vulkan::Device *device = nullptr;
	FFmpegDecode::Shaders<> shaders;
	Audio::Mixer *mixer = nullptr;
	DecodeOptions opts;
	AVFormatContext *av_format_ctx = nullptr;
	AVPacket *av_pkt = nullptr;
	CodecStream video, audio;

	enum class ImageState
	{
		Idle, // Was released by application.
		Locked, // Decode thread locked this image.
		Ready, // Can be acquired.
		Acquired // Acquired, can be released.
	};

	struct DecodedImage
	{
		Vulkan::ImageHandle rgb_image;
		Vulkan::ImageViewHandle rgb_storage_view;
		Vulkan::ImageHandle planes[3];

		Vulkan::Semaphore sem_to_client;
		Vulkan::Semaphore sem_from_client;
		uint64_t idle_order = 0;
		uint64_t lock_order = 0;

		double pts = 0.0;
		uint64_t done_ts = 0;
		ImageState state = ImageState::Idle;
	};
	std::vector<DecodedImage> video_queue;
	uint64_t idle_timestamps = 0;
	bool is_video_eof = false;
	bool is_audio_eof = false;
	bool is_flushing = false;
	bool acquire_is_eof = false;

	VkFormat plane_formats[3] = {};
	unsigned plane_subsample_log2[3] = {};
	unsigned num_planes = 0;
	Vulkan::Program *program = nullptr;

	bool init(Audio::Mixer *mixer, const char *path, const DecodeOptions &opts);
	unsigned get_width() const;
	unsigned get_height() const;
	bool init_video_decoder_pre_device();
	bool init_video_decoder_post_device();
	bool init_audio_decoder();

	bool begin_device_context(Vulkan::Device *device, const FFmpegDecode::Shaders<> &shaders);
	void end_device_context();
	bool play();
	bool get_stream_id(Audio::StreamID &id) const;
	bool stop();
	bool seek(double ts);
	void set_paused(bool enable);
	bool get_paused() const;

	double get_estimated_audio_playback_timestamp(double elapsed_time);
	double latch_estimated_video_playback_timestamp(double elapsed_time, double target_latency);
	void latch_estimated_audio_playback_timestamp(double pts);
	void set_audio_delta_rate_factor(float delta);
	double get_audio_buffering_duration();
	double get_last_video_buffering_pts();
	unsigned get_num_ready_video_frames();
	double get_estimated_audio_playback_timestamp_raw();
	void latch_audio_buffering_target(double target_buffer_time);

	bool acquire_video_frame(VideoFrame &frame, int timeout_ms);
	int try_acquire_video_frame(VideoFrame &frame);
	bool is_eof();
	void release_video_frame(unsigned index, Vulkan::Semaphore sem);

	bool decode_video_packet(AVPacket *pkt);
	bool decode_audio_packet(AVPacket *pkt);
	bool drain_video_frame();
	bool drain_audio_frame();

	int find_idle_decode_video_frame_locked() const;
	int find_acquire_video_frame_locked() const;

	unsigned acquire_decode_video_frame();
	void process_video_frame(AVFrame *frame);
	void process_video_frame_in_task(unsigned frame, AVFrame *av_frame);

	void dispatch_conversion(Vulkan::CommandBuffer &cmd, DecodedImage &img, const Vulkan::ImageView * const *views);
	void process_video_frame_in_task_upload(DecodedImage &img, AVFrame *av_frame, Vulkan::Semaphore &compute_to_user);
#ifdef HAVE_FFMPEG_VULKAN
	void process_video_frame_in_task_vulkan(DecodedImage &img, AVFrame *av_frame, Vulkan::Semaphore &compute_to_user);
#endif

	void flush_codecs();

	struct UBO
	{
		mat4 yuv_to_rgb;
		mat4 primary_conversion;
		uvec2 resolution;
		vec2 inv_resolution;
		vec2 chroma_siting;
		vec2 chroma_clamp;
		float unorm_rescale;
	};
	UBO ubo = {};

	// The decoding thread.
	std::thread decode_thread;
	std::condition_variable cond;
	std::mutex lock;
	std::mutex iteration_lock;
	bool teardown = false;
	bool acquire_blocking = false;
	TaskSignal video_upload_signal;
	uint64_t video_upload_count = 0;
	ThreadGroup *thread_group = nullptr;
	TaskGroupHandle upload_dependency;
	void thread_main();
	bool iterate();
	bool should_iterate_locked();

	void init_yuv_to_rgb();
	void setup_yuv_format_planes();
	void begin_audio_stream();
	AVPixelFormat active_upload_pix_fmt = AV_PIX_FMT_NONE;
	AVColorSpace active_color_space = AVCOL_SPC_UNSPECIFIED;

	~Impl();

#ifdef HAVE_GRANITE_AUDIO
	Audio::StreamID stream_id;
	AVFrameRingStream *stream = nullptr;
#endif

	FFmpegHWDevice hw;

	bool is_paused = false;

	double smooth_elapsed = 0.0;
	double smooth_pts = 0.0;
	DemuxerIOInterface *io_interface = nullptr;

	pyro_codec_parameters pyro_codec = {};
	bool has_observed_keyframe = false;
	int read_frame(AVPacket *av_pkt);

	Granite::Global::GlobalManagersHandle managers;
};

int VideoDecoder::Impl::find_idle_decode_video_frame_locked() const
{
	int best_index = -1;
	for (size_t i = 0, n = video_queue.size(); i < n; i++)
	{
		auto &img = video_queue[i];
		if (img.state == ImageState::Idle &&
		    (best_index < 0 || img.idle_order < video_queue[best_index].idle_order))
		{
			best_index = int(i);
		}
	}

	return best_index;
}

unsigned VideoDecoder::Impl::acquire_decode_video_frame()
{
	int best_index;

	do
	{
		std::unique_lock<std::mutex> holder{lock};
		best_index = find_idle_decode_video_frame_locked();

		// We have no choice but to trample on a frame we already decoded.
		// This can happen if audio is running ahead for whatever reason,
		// and we need to start catching up due to massive stutters or similar.
		// For this reason, we should consume the produced image with lowest PTS.
		if (best_index < 0)
		{
			for (size_t i = 0, n = video_queue.size(); i < n; i++)
			{
				if (video_queue[i].state == ImageState::Ready &&
				    (best_index < 0 || video_queue[i].pts < video_queue[best_index].pts))
				{
					best_index = int(i);
					LOGW("FFmpeg decode: Trampling on decoded frame.\n");
				}
			}
		}

		// We have completely stalled.
		if (best_index < 0)
		{
			uint64_t wait_count = UINT64_MAX;
			for (size_t i = 0, n = video_queue.size(); i < n; i++)
				if (video_queue[i].state == ImageState::Locked)
					wait_count = std::min<uint64_t>(wait_count, video_queue[i].lock_order);

			// Completing the task needs to take lock.
			holder.unlock();

			// Could happen if application is acquiring images beyond all reason.
			VK_ASSERT(wait_count != UINT64_MAX);
			if (wait_count != UINT64_MAX)
				video_upload_signal.wait_until_at_least(wait_count);
		}
	} while (best_index < 0);

	auto &img = video_queue[best_index];

	// Defer allocating the planar images until we know for sure what kind of
	// format we're dealing with.
	if (!img.rgb_image)
	{
		auto info = Vulkan::ImageCreateInfo::immutable_2d_image(video.av_ctx->width,
		                                                        video.av_ctx->height,
		                                                        VK_FORMAT_R8G8B8A8_SRGB);
		info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
		             VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		info.flags = VK_IMAGE_CREATE_EXTENDED_USAGE_BIT;
		info.misc = Vulkan::IMAGE_MISC_CONCURRENT_QUEUE_GRAPHICS_BIT |
		            Vulkan::IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_COMPUTE_BIT |
		            Vulkan::IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_TRANSFER_BIT |
		            Vulkan::IMAGE_MISC_MUTABLE_SRGB_BIT;
		if (opts.mipgen)
			info.levels = 0;
		img.rgb_image = device->create_image(info);

		Vulkan::ImageViewCreateInfo view = {};
		view.image = img.rgb_image.get();
		view.format = VK_FORMAT_R8G8B8A8_UNORM;
		view.layers = 1;
		view.levels = 1;
		view.view_type = VK_IMAGE_VIEW_TYPE_2D;
		img.rgb_storage_view = device->create_image_view(view);
	}

	return best_index;
}

void VideoDecoder::Impl::init_yuv_to_rgb()
{
	ubo.resolution = uvec2(video.av_ctx->width, video.av_ctx->height);

	if (video.av_ctx->hw_frames_ctx && hw.get_hw_device_type() == AV_HWDEVICE_TYPE_VULKAN)
	{
		// Frames may be padded.
		auto *frames = reinterpret_cast<AVHWFramesContext *>(video.av_ctx->hw_frames_ctx->data);
		ubo.inv_resolution = vec2(1.0f / float(frames->width), 1.0f / float(frames->height));
	}
	else
	{
		ubo.inv_resolution = vec2(1.0f / float(video.av_ctx->width),
		                          1.0f / float(video.av_ctx->height));
	}

	ubo.chroma_clamp = (vec2(ubo.resolution) - 0.5f * float(1u << plane_subsample_log2[1])) * ubo.inv_resolution;
	const char *siting;

	switch (video.av_ctx->chroma_sample_location)
	{
	case AVCHROMA_LOC_TOPLEFT:
		ubo.chroma_siting = vec2(1.0f);
		siting = "TopLeft";
		break;

	case AVCHROMA_LOC_TOP:
		ubo.chroma_siting = vec2(0.5f, 1.0f);
		siting = "Top";
		break;

	case AVCHROMA_LOC_LEFT:
		ubo.chroma_siting = vec2(1.0f, 0.5f);
		siting = "Left";
		break;

	case AVCHROMA_LOC_CENTER:
	default:
		ubo.chroma_siting = vec2(0.5f);
		siting = "Center";
		break;

	case AVCHROMA_LOC_BOTTOMLEFT:
		ubo.chroma_siting = vec2(1.0f, 0.0f);
		siting = "BottomLeft";
		break;

	case AVCHROMA_LOC_BOTTOM:
		ubo.chroma_siting = vec2(0.5f, 0.0f);
		siting = "Bottom";
		break;
	}

	bool full_range = video.av_ctx->color_range == AVCOL_RANGE_JPEG;
	LOGI("Range: %s\n", full_range ? "full" : "limited");
	LOGI("Chroma: %s\n", siting);

	// 16.3.9 from Vulkan spec.
	// YCbCr samplers is not universally supported,
	// so we need to do this translation ourselves.
	// This is ok, since we have to do EOTF and primary conversion manually either way,
	// and those are not supported.

	int luma_offset = full_range ? 0 : 16;
	int chroma_narrow_range = 224;
	int luma_narrow_range = 219;
	int bit_depth = av_pix_fmt_desc_get(active_upload_pix_fmt)->comp[0].depth;
	if (bit_depth > 8)
	{
		luma_offset <<= (bit_depth - 8);
		luma_narrow_range <<= (bit_depth - 8);
		chroma_narrow_range <<= (bit_depth - 8);
	}

	// 10-bit and 12-bit YUV need special consideration for how to do scale and bias.
	float midpoint = float(1 << (bit_depth - 1));
	float unorm_range = float((1 << bit_depth) - 1);
	float unorm_divider = 1.0f / unorm_range;
	float chroma_shift = -midpoint * unorm_divider;

	const float luma_scale = float(unorm_range) / float(luma_narrow_range);
	const float chroma_scale = float(unorm_range) / float(chroma_narrow_range);

	const vec3 yuv_bias = vec3(float(-luma_offset) * unorm_divider, chroma_shift, chroma_shift);
	const vec3 yuv_scale = full_range ? vec3(1.0f) :
	                       vec3(luma_scale, chroma_scale, chroma_scale);

	AVColorSpace col_space = video.av_ctx->colorspace;
	if (col_space == AVCOL_SPC_UNSPECIFIED)
	{
		// The common case is when we have an unspecified color space.
		// We have to deduce the color space based on resolution since NTSC, PAL, HD and UHD all
		// have different conversions.
		if (video.av_ctx->height < 625)
			col_space = AVCOL_SPC_SMPTE170M; // 525 line NTSC
		else if (video.av_ctx->height < 720)
			col_space = AVCOL_SPC_BT470BG; // 625 line PAL
		else if (video.av_ctx->height < 2160)
			col_space = AVCOL_SPC_BT709; // BT709 HD
		else
			col_space = AVCOL_SPC_BT2020_CL; // UHD
	}

	// Khronos Data Format Specification 15.1.1:

	// EOTF is based on BT.2087 which recommends that an approximation to BT.1886 is used
	// for purposes of color conversion.
	// E = pow(E', 2.4).
	// We apply this to everything for now, but might not be correct for SD content, especially PAL.
	// Can be adjusted as needed with spec constants.
	// AVCodecContext::color_rtc can signal a specific EOTF,
	// but I've only seen UNSPECIFIED here.

	const Primaries bt709 = {
		{ 0.640f, 0.330f },
		{ 0.300f, 0.600f },
		{ 0.150f, 0.060f },
		{ 0.3127f, 0.3290f },
	};

	const Primaries bt601_625 = {
		{ 0.640f, 0.330f },
		{ 0.290f, 0.600f },
		{ 0.150f, 0.060f },
		{ 0.3127f, 0.3290f },
	};

	const Primaries bt601_525 = {
		{ 0.630f, 0.340f },
		{ 0.310f, 0.595f },
		{ 0.155f, 0.070f },
		{ 0.3127f, 0.3290f },
	};

	const Primaries bt2020 = {
		{ 0.708f, 0.292f },
		{ 0.170f, 0.797f },
		{ 0.131f, 0.046f },
		{ 0.3127f, 0.3290f },
	};

	active_color_space = col_space;

	switch (col_space)
	{
	default:
		LOGW("Unknown color space: %u, assuming BT.709.\n", col_space);
		// fallthrough
	case AVCOL_SPC_BT709:
		LOGI("BT.709 color space.\n");
		ubo.yuv_to_rgb = mat4(mat3(vec3(1.0f, 1.0f, 1.0f),
		                           vec3(0.0f, -0.13397432f / 0.7152f, 1.8556f),
		                           vec3(1.5748f, -0.33480248f / 0.7152f, 0.0f)));
		ubo.primary_conversion = mat4(1.0f); // sRGB shares primaries.
		break;

	case AVCOL_SPC_BT2020_CL:
	case AVCOL_SPC_BT2020_NCL:
		LOGI("BT.2020 color space.\n");
		ubo.yuv_to_rgb = mat4(mat3(vec3(1.0f, 1.0f, 1.0f),
		                           vec3(0.0f, -0.11156702f / 0.6780f, 1.8814f),
		                           vec3(1.4746f, -0.38737742f / 0.6780f, 0.0f)));
		ubo.primary_conversion = mat4(inverse(compute_xyz_matrix(bt709)) * compute_xyz_matrix(bt2020));
		break;

	case AVCOL_SPC_SMPTE170M:
	case AVCOL_SPC_BT470BG:
		LOGI("BT.601 color space.\n");
		// BT.601. Primaries differ between EBU and SMPTE.
		ubo.yuv_to_rgb = mat4(mat3(vec3(1.0f, 1.0f, 1.0f),
		                           vec3(0.0f, -0.202008f / 0.587f, 1.772f),
		                           vec3(1.402f, -0.419198f / 0.587f, 0.0f)));
		ubo.primary_conversion = mat4(inverse(compute_xyz_matrix(bt709)) *
		                              compute_xyz_matrix(col_space == AVCOL_SPC_BT470BG ? bt601_625 : bt601_525));
		break;

	case AVCOL_SPC_SMPTE240M:
		LOGI("SMPTE240M color space.\n");
		// This does not seem to have a corresponding model in Vulkan.
		ubo.yuv_to_rgb = mat4(mat3(vec3(1.0f, 1.0f, 1.0f),
		                           vec3(0.0f, -0.58862f / 0.701f, 1.826f),
		                           vec3(1.576f, -0.334112f / 0.701f, 0.0f)));
		ubo.primary_conversion = mat4(inverse(compute_xyz_matrix(bt709)) *
		                              compute_xyz_matrix(bt601_525));
		break;
	}

	ubo.yuv_to_rgb = ubo.yuv_to_rgb * scale(yuv_scale) * translate(yuv_bias);
}

bool VideoDecoder::Impl::init_audio_decoder()
{
	// This is fine. We can support no-audio files.
	int ret;

	if (av_format_ctx)
	{
		if ((ret = av_find_best_stream(av_format_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0)) < 0)
			return true;

		audio.av_stream = av_format_ctx->streams[ret];
	}

	const AVCodec *codec = nullptr;

	if (audio.av_stream)
	{
		codec = avcodec_find_decoder(audio.av_stream->codecpar->codec_id);
	}
	else
	{
		switch (pyro_codec.audio_codec)
		{
		case PYRO_AUDIO_CODEC_OPUS:
			codec = avcodec_find_decoder(AV_CODEC_ID_OPUS);
			break;

		case PYRO_AUDIO_CODEC_AAC:
			codec = avcodec_find_decoder(AV_CODEC_ID_AAC);
			break;

		case PYRO_AUDIO_CODEC_RAW_S16LE:
			codec = avcodec_find_decoder(AV_CODEC_ID_PCM_S16LE);
			break;

		case PYRO_AUDIO_CODEC_NONE:
			return true;

		default:
			LOGE("Unknown audio codec.\n");
			return false;
		}
	}

	if (!codec)
	{
		LOGE("Failed to find codec.\n");
		return false;
	}

	audio.av_ctx = avcodec_alloc_context3(codec);
	if (!audio.av_ctx)
	{
		LOGE("Failed to allocate codec context.\n");
		return false;
	}

	const AVChannelLayout mono = AV_CHANNEL_LAYOUT_MONO;
	const AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
	if (audio.av_stream)
	{
		if (avcodec_parameters_to_context(audio.av_ctx, audio.av_stream->codecpar) < 0)
		{
			LOGE("Failed to copy codec parameters.\n");
			return false;
		}
	}
	else
	{
		audio.av_ctx->sample_rate = pyro_codec.rate;
		if (pyro_codec.channels == 2)
			audio.av_ctx->ch_layout = AV_CHANNEL_LAYOUT_STEREO;
		else if (pyro_codec.channels == 1)
			audio.av_ctx->ch_layout = AV_CHANNEL_LAYOUT_MONO;
		else
		{
			LOGE("Unexpected audio channel count %u.\n", pyro_codec.channels);
			return false;
		}
	}

	if (avcodec_open2(audio.av_ctx, codec, nullptr) < 0)
	{
		LOGE("Failed to open codec.\n");
		return false;
	}

	if (av_channel_layout_compare(&audio.av_ctx->ch_layout, &mono) != 0 &&
	    av_channel_layout_compare(&audio.av_ctx->ch_layout, &stereo) != 0)
	{
		LOGE("Unrecognized audio channel layout.\n");
		avcodec_free_context(&audio.av_ctx);
		audio.av_stream = nullptr;
		return true;
	}

	switch (audio.av_ctx->sample_fmt)
	{
	case AV_SAMPLE_FMT_S16:
	case AV_SAMPLE_FMT_S16P:
	case AV_SAMPLE_FMT_S32:
	case AV_SAMPLE_FMT_S32P:
	case AV_SAMPLE_FMT_FLT:
	case AV_SAMPLE_FMT_FLTP:
		break;

	default:
		LOGE("Unsupported sample format.\n");
		return false;
	}

	return true;
}

void VideoDecoder::Impl::begin_audio_stream()
{
#ifdef HAVE_GRANITE_AUDIO
	if (!audio.av_ctx)
		return;

	double time_base;
	if (audio.av_stream)
		time_base = av_q2d(audio.av_stream->time_base);
	else
		time_base = 1e-6;

	stream = new AVFrameRingStream(
			float(audio.av_ctx->sample_rate),
#ifdef ch_layout
			audio.av_ctx->channels,
#else
			audio.av_ctx->ch_layout.nb_channels,
#endif
			time_base, opts.realtime, opts.blocking);

	stream->add_reference();
	stream_id = mixer->add_mixer_stream(stream, !is_paused);
	if (!stream_id)
	{
		stream->release_reference();
		stream = nullptr;
	}

	// Reset PTS smoothing.
	smooth_elapsed = 0.0;
	smooth_pts = 0.0;
#endif
}

bool VideoDecoder::Impl::init_video_decoder_post_device()
{
	if (!hw.init_codec_context(video.av_codec, device, video.av_ctx, opts.hwdevice, false))
		LOGW("Failed to init hardware decode context. Falling back to software.\n");

	if (avcodec_open2(video.av_ctx, video.av_codec, nullptr) < 0)
	{
		LOGE("Failed to open codec.\n");
		return false;
	}

	double fps;
	if (video.av_stream)
		fps = av_q2d(video.av_stream->avg_frame_rate);
	else
	{
		AVRational q;
		q.num = pyro_codec.frame_rate_num;
		q.den = pyro_codec.frame_rate_den;
		fps = av_q2d(q);
	}

	// If FPS is not specified assume 60 as a "worst case scenario".
	if (fps == 0.0)
		fps = 60.0;

	// We need to buffer up enough frames without running into starvation scenarios.
	// The low watermark for audio buffer is 100ms, which is where we will start forcing video frames to be decoded.
	// If we allocate 200ms of video frames to absorb any jank, we should be fine.
	// In a steady state, we will keep the audio buffer at 200ms saturation.
	// It would be possible to add new video frames dynamically,
	// but we don't want to end up in an unbounded memory usage situation, especially VRAM.
	unsigned num_frames = std::max<unsigned>(unsigned(muglm::ceil(fps * opts.target_video_buffer_time)), 8);

	video_queue.resize(num_frames);

	return true;
}

bool VideoDecoder::Impl::init_video_decoder_pre_device()
{
	int ret;

	if (av_format_ctx)
	{
		if ((ret = av_find_best_stream(av_format_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0)) < 0)
		{
			LOGE("Failed to find best stream.\n");
			return false;
		}

		video.av_stream = av_format_ctx->streams[ret];
	}

	const AVCodec *codec = nullptr;

	if (av_format_ctx)
	{
		codec = avcodec_find_decoder(video.av_stream->codecpar->codec_id);
	}
	else if (io_interface)
	{
		switch (pyro_codec.video_codec)
		{
		case PYRO_VIDEO_CODEC_H264:
			codec = avcodec_find_decoder(AV_CODEC_ID_H264);
			break;

		case PYRO_VIDEO_CODEC_H265:
			codec = avcodec_find_decoder(AV_CODEC_ID_H265);
			break;

		case PYRO_VIDEO_CODEC_AV1:
			codec = avcodec_find_decoder(AV_CODEC_ID_AV1);
			break;

		default:
			LOGE("Unknown video codec.\n");
			return false;
		}
	}

	if (!codec)
	{
		LOGE("Failed to find codec.\n");
		return false;
	}

	video.av_codec = codec;
	video.av_ctx = avcodec_alloc_context3(codec);
	if (!video.av_ctx)
	{
		LOGE("Failed to allocate codec context.\n");
		return false;
	}

	if (video.av_stream)
	{
		if (avcodec_parameters_to_context(video.av_ctx, video.av_stream->codecpar) < 0)
		{
			LOGE("Failed to copy codec parameters.\n");
			return false;
		}
	}
	else
	{
		video.av_ctx->width = pyro_codec.width;
		video.av_ctx->height = pyro_codec.height;
		video.av_ctx->framerate.num = pyro_codec.frame_rate_num;
		video.av_ctx->framerate.den = pyro_codec.frame_rate_den;
		// Packet loss is expected, and we'd rather have something on screen than nothing.
		video.av_ctx->flags |= AV_CODEC_FLAG_OUTPUT_CORRUPT;

		// TODO: Make this configurable in pyro protocol.
		// This is default H.264 / H.265 for HD content.
		video.av_ctx->color_primaries = AVCOL_PRI_BT709;
		video.av_ctx->color_range = AVCOL_RANGE_MPEG;
		video.av_ctx->colorspace = AVCOL_SPC_BT709;
		video.av_ctx->chroma_sample_location = AVCHROMA_LOC_LEFT;
	}

	video.av_ctx->opaque = &hw;
	return true;
}

unsigned VideoDecoder::Impl::get_width() const
{
	return video.av_ctx->width;
}

unsigned VideoDecoder::Impl::get_height() const
{
	return video.av_ctx->height;
}

bool VideoDecoder::Impl::init(Audio::Mixer *mixer_, const char *path, const DecodeOptions &opts_)
{
	mixer = mixer_;
	opts = opts_;
	managers = Granite::Global::create_thread_context();

	if (!io_interface)
	{
		if (avformat_open_input(&av_format_ctx, path, nullptr, nullptr) < 0)
		{
			LOGE("Failed to open input %s.\n", path);
			return false;
		}

		if (avformat_find_stream_info(av_format_ctx, nullptr) < 0)
		{
			LOGE("Failed to find stream info.\n");
			return false;
		}
	}
	else
	{
		pyro_codec = io_interface->get_codec_parameters();
		if (pyro_codec.video_codec == PYRO_VIDEO_CODEC_NONE)
		{
			LOGE("Failed to get raw codec parameters.\n");
			return false;
		}
	}

	if (!init_video_decoder_pre_device())
		return false;
	if (mixer && !init_audio_decoder())
		return false;

	if (!(av_pkt = av_packet_alloc()))
	{
		LOGE("Failed to allocate packet.\n");
		return false;
	}

	return true;
}

int VideoDecoder::Impl::find_acquire_video_frame_locked() const
{
	// Want frame with lowest PTS and in Ready state.
	int best_index = -1;
	for (size_t i = 0, n = video_queue.size(); i < n; i++)
	{
		auto &img = video_queue[i];
		if (img.state == ImageState::Ready &&
		    (best_index < 0 || (img.pts < video_queue[best_index].pts)))
		{
			best_index = int(i);
		}
	}

	return best_index;
}

void VideoDecoder::Impl::setup_yuv_format_planes()
{
	// TODO: Is there a way to make this data driven from the FFmpeg API?
	// In practice, this isn't going to be used as a fully general purpose
	// media player, so we only need to consider the FMVs that an application ships.

	ubo.unorm_rescale = 1.0f;

	switch (active_upload_pix_fmt)
	{
	case AV_PIX_FMT_YUV444P:
	case AV_PIX_FMT_YUV420P:
		plane_formats[0] = VK_FORMAT_R8_UNORM;
		plane_formats[1] = VK_FORMAT_R8_UNORM;
		plane_formats[2] = VK_FORMAT_R8_UNORM;
		plane_subsample_log2[0] = 0;
		plane_subsample_log2[1] = active_upload_pix_fmt == AV_PIX_FMT_YUV420P ? 1 : 0;
		plane_subsample_log2[2] = active_upload_pix_fmt == AV_PIX_FMT_YUV420P ? 1 : 0;
		num_planes = 3;
		break;

	case AV_PIX_FMT_NV12:
	case AV_PIX_FMT_NV21:
		// NV21 is done by spec constant swizzle.
		plane_formats[0] = VK_FORMAT_R8_UNORM;
		plane_formats[1] = VK_FORMAT_R8G8_UNORM;
		num_planes = 2;
		plane_subsample_log2[0] = 0;
		plane_subsample_log2[1] = 1;
		break;

	case AV_PIX_FMT_P010:
#ifdef AV_PIX_FMT_P410
	case AV_PIX_FMT_P410:
#endif
		plane_formats[0] = VK_FORMAT_R16_UNORM;
		plane_formats[1] = VK_FORMAT_R16G16_UNORM;
		num_planes = 2;
		plane_subsample_log2[0] = 0;
		plane_subsample_log2[1] = active_upload_pix_fmt == AV_PIX_FMT_P010 ? 1 : 0;
		// The low bits are zero, rescale to 1.0 range (could there be garbage here on hardware decoders?).
		ubo.unorm_rescale = float(0xffff) / float(1023 << 6);

		if (device->image_format_is_supported(VK_FORMAT_R10X6_UNORM_PACK16, VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT) &&
		    device->image_format_is_supported(VK_FORMAT_R10X6G10X6_UNORM_2PACK16, VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT))
		{
			// Avoid any potential issue with garbage in HW decoders.
			plane_formats[0] = VK_FORMAT_R10X6_UNORM_PACK16;
			plane_formats[1] = VK_FORMAT_R10X6G10X6_UNORM_2PACK16;
			ubo.unorm_rescale = 1.0f;
		}
		break;

	case AV_PIX_FMT_YUV420P10:
	case AV_PIX_FMT_YUV444P10:
		plane_formats[0] = VK_FORMAT_R16_UNORM;
		plane_formats[1] = VK_FORMAT_R16_UNORM;
		plane_formats[2] = VK_FORMAT_R16_UNORM;
		num_planes = 3;
		plane_subsample_log2[0] = 0;
		plane_subsample_log2[1] = active_upload_pix_fmt == AV_PIX_FMT_YUV420P10 ? 1 : 0;
		plane_subsample_log2[2] = active_upload_pix_fmt == AV_PIX_FMT_YUV420P10 ? 1 : 0;
		// The high bits are zero, rescale to 1.0 range.
		// This format is only returned by software decoding.
		ubo.unorm_rescale = float(0xffff) / float(1023);
		break;

	case AV_PIX_FMT_P016:
#ifdef AV_PIX_FMT_P416
	case AV_PIX_FMT_P416:
#endif
		plane_formats[0] = VK_FORMAT_R16_UNORM;
		plane_formats[1] = VK_FORMAT_R16G16_UNORM;
		num_planes = 2;
		plane_subsample_log2[0] = 0;
		plane_subsample_log2[1] = active_upload_pix_fmt == AV_PIX_FMT_P016 ? 1 : 0;
		plane_subsample_log2[2] = active_upload_pix_fmt == AV_PIX_FMT_P016 ? 1 : 0;
		break;

	default:
		LOGE("Unrecognized pixel format: %d.\n", active_upload_pix_fmt);
		num_planes = 0;
		break;
	}

	init_yuv_to_rgb();

	program = shaders.yuv_to_rgb;
}

#ifdef HAVE_FFMPEG_VULKAN
void VideoDecoder::Impl::process_video_frame_in_task_vulkan(DecodedImage &img, AVFrame *av_frame,
                                                            Vulkan::Semaphore &compute_to_user)
{
	auto *frames = reinterpret_cast<AVHWFramesContext *>(video.av_ctx->hw_frames_ctx->data);
	auto *vk = static_cast<AVVulkanFramesContext *>(frames->hwctx);
	auto *vk_frame = reinterpret_cast<AVVkFrame *>(av_frame->data[0]);

	// Docs suggest we have to lock the AVVkFrame when accessing the frame struct.
	struct FrameLock
	{
		AVHWFramesContext *frames;
		AVVulkanFramesContext *vk;
		AVVkFrame *vk_frame;
		inline void lock() const { vk->lock_frame(frames, vk_frame); }
		inline void unlock() const { vk->unlock_frame(frames, vk_frame); }
	} l = { frames, vk, vk_frame };
	std::lock_guard<FrameLock> holder{l};

	// We're not guaranteed to receive the same VkImages over and over, so
	// just recreate the views and throw them away every iteration.

	Vulkan::ImageCreateInfo info = {};
	info.type = VK_IMAGE_TYPE_2D;
	// Extent parameters aren't necessarily quite correct,
	// but we don't really care since we're just creating temporary views.
	info.width = video.av_ctx->width;
	info.height = video.av_ctx->height;
	info.depth = 1;
	info.format = vk->format[0];
	info.usage = vk->usage;
	info.flags = vk->img_flags;
	info.layers = 1;
	info.levels = 1;
	info.domain = Vulkan::ImageDomain::Physical;
	info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;

	// Apparently, we are guaranteed a single multi-plane image here.
	auto wrapped_image = device->wrap_image(info, vk_frame->img[0]);

	Vulkan::ImageViewCreateInfo view_info = {};
	view_info.image = wrapped_image.get();
	view_info.view_type = VK_IMAGE_VIEW_TYPE_2D;
	Vulkan::ImageViewHandle planes[3];

	for (unsigned i = 0; i < num_planes; i++)
	{
		view_info.format = plane_formats[i];
		view_info.aspect = VK_IMAGE_ASPECT_PLANE_0_BIT << i;
		planes[i] = device->create_image_view(view_info);
	}

	auto conversion_queue = opts.mipgen ?
	                        Vulkan::CommandBuffer::Type::Generic :
	                        Vulkan::CommandBuffer::Type::AsyncCompute;

	if (img.sem_from_client)
	{
		device->add_wait_semaphore(conversion_queue,
		                           std::move(img.sem_from_client),
		                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		                           true);
		img.sem_from_client = {};
	}

	if (vk_frame->queue_family[0] != VK_QUEUE_FAMILY_IGNORED)
		LOGW("Unexpected queue family in Vulkan video processing.\n");

	Vulkan::Semaphore wrapped_timeline;
	if (vk_frame->sem[0] != VK_NULL_HANDLE)
		wrapped_timeline = device->request_semaphore(VK_SEMAPHORE_TYPE_TIMELINE, vk_frame->sem[0], false);

	// Acquire the image from FFmpeg.
	if (vk_frame->sem[0] != VK_NULL_HANDLE && vk_frame->sem_value[0])
	{
		auto timeline = device->request_timeline_semaphore_as_binary(
				*wrapped_timeline, vk_frame->sem_value[0]);
		timeline->signal_external();
		device->add_wait_semaphore(conversion_queue,
		                           std::move(timeline),
		                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		                           true);
	}

	auto cmd = device->request_command_buffer(conversion_queue);

	cmd->image_barrier(*wrapped_image,
	                   vk_frame->layout[0], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
	                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
	vk_frame->layout[0] = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	const Vulkan::ImageView *views[] = { planes[0].get(), planes[1].get(), planes[2].get() };
	dispatch_conversion(*cmd, img, views);

	device->submit(cmd, nullptr, 1, &compute_to_user);

	// Release the image back to FFmpeg.
	if (vk_frame->sem[0] != VK_NULL_HANDLE)
	{
		auto timeline = device->request_timeline_semaphore_as_binary(
				*wrapped_timeline, ++vk_frame->sem_value[0]);
		device->submit_empty(conversion_queue, nullptr, timeline.get());
	}
}
#endif

void VideoDecoder::Impl::dispatch_conversion(Vulkan::CommandBuffer &cmd, DecodedImage &img, const Vulkan::ImageView *const *views)
{
	if (num_planes)
	{
		cmd.set_storage_texture(0, 0, *img.rgb_storage_view);

		for (unsigned i = 0; i < num_planes; i++)
		{
			cmd.set_texture(0, 1 + i, *views[i],
			                i == 0 ? Vulkan::StockSampler::NearestClamp : Vulkan::StockSampler::LinearClamp);
		}
		for (unsigned i = num_planes; i < 3; i++)
			cmd.set_texture(0, 1 + i, *views[0], Vulkan::StockSampler::NearestClamp);

		cmd.set_program(program);

		cmd.set_specialization_constant_mask(7u);
		cmd.set_specialization_constant(0, uint32_t(active_color_space != AVCOL_SPC_BT709));
		cmd.set_specialization_constant(1, num_planes);
		cmd.set_specialization_constant(2, uint32_t(active_upload_pix_fmt == AV_PIX_FMT_NV21));

		memcpy(cmd.allocate_typed_constant_data<UBO>(1, 0, 1), &ubo, sizeof(ubo));

		cmd.image_barrier(*img.rgb_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
		                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
		                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
		cmd.dispatch((ubo.resolution.x + 7) / 8, (ubo.resolution.y + 7) / 8, 1);

		if (opts.mipgen)
		{
			cmd.barrier_prepare_generate_mipmap(*img.rgb_image, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			                                    VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, true);
			cmd.generate_mipmap(*img.rgb_image);
			cmd.image_barrier(*img.rgb_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			                  VK_PIPELINE_STAGE_2_BLIT_BIT, 0,
			                  VK_PIPELINE_STAGE_NONE, 0);
		}
		else
		{
			cmd.image_barrier(*img.rgb_image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
			                  VK_PIPELINE_STAGE_NONE, 0);
		}
	}
	else
	{
		// Fallback, just clear to magenta to make it obvious what went wrong.
		cmd.image_barrier(*img.rgb_image,
		                  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
		                  VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);

		VkClearValue color = {};
		color.color.float32[0] = 1.0f;
		color.color.float32[2] = 1.0f;
		color.color.float32[3] = 1.0f;
		cmd.clear_image(*img.rgb_image, color);
		cmd.image_barrier(*img.rgb_image,
		                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		                  VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
		                  VK_PIPELINE_STAGE_NONE, 0);
	}
}

void VideoDecoder::Impl::process_video_frame_in_task_upload(DecodedImage &img, AVFrame *av_frame,
                                                            Vulkan::Semaphore &compute_to_user)
{
	for (unsigned i = 0; i < num_planes; i++)
	{
		auto &plane = img.planes[i];
		if (!plane)
		{
			auto info = Vulkan::ImageCreateInfo::immutable_2d_image(
					video.av_ctx->width >> plane_subsample_log2[i],
					video.av_ctx->height >> plane_subsample_log2[i],
					plane_formats[i]);
			info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
			info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
			info.misc = Vulkan::IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_COMPUTE_BIT |
			            Vulkan::IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_TRANSFER_BIT;
			plane = device->create_image(info);
		}
	}

	Vulkan::Semaphore transfer_to_compute;

	if (img.sem_from_client)
	{
		device->add_wait_semaphore(Vulkan::CommandBuffer::Type::AsyncTransfer,
		                           std::move(img.sem_from_client),
		                           VK_PIPELINE_STAGE_2_COPY_BIT,
		                           true);
		img.sem_from_client = {};
	}

	auto cmd = device->request_command_buffer(Vulkan::CommandBuffer::Type::AsyncTransfer);

	for (unsigned i = 0; i < num_planes; i++)
	{
		cmd->image_barrier(*img.planes[i],
		                   VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		                   VK_PIPELINE_STAGE_2_COPY_BIT, 0,
		                   VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
	}

	for (unsigned i = 0; i < num_planes; i++)
	{
		auto *buf = static_cast<uint8_t *>(cmd->update_image(*img.planes[i]));
		int byte_width = int(img.planes[i]->get_width());
		byte_width *= int(Vulkan::TextureFormatLayout::format_block_size(plane_formats[i], VK_IMAGE_ASPECT_COLOR_BIT));

		av_image_copy_plane(buf, byte_width,
		                    av_frame->data[i], av_frame->linesize[i],
		                    byte_width, int(img.planes[i]->get_height()));
	}

	for (unsigned i = 0; i < num_planes; i++)
	{
		cmd->image_barrier(*img.planes[i],
		                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		                   VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
		                   VK_PIPELINE_STAGE_NONE, 0);
	}

	device->submit(cmd, nullptr, 1, &transfer_to_compute);

	auto conversion_queue = opts.mipgen ?
	                        Vulkan::CommandBuffer::Type::Generic :
	                        Vulkan::CommandBuffer::Type::AsyncCompute;

	device->add_wait_semaphore(conversion_queue,
	                           std::move(transfer_to_compute),
	                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
	                           true);

	cmd = device->request_command_buffer(conversion_queue);

	const Vulkan::ImageView *views[3] = {};
	for (unsigned i = 0; i < num_planes; i++)
		views[i] = &img.planes[i]->get_view();

	dispatch_conversion(*cmd, img, views);

	device->submit(cmd, nullptr, 1, &compute_to_user);

	// When running in realtime mode we will run
	// completely unlocked from the main loop, so make sure
	// we don't leak unbounded memory when the window is minimized on Windows.
	// In that scenario the main thread will not pump frame contexts regularly.
	if (opts.realtime)
		device->next_frame_context_in_async_thread();
}

void VideoDecoder::Impl::process_video_frame_in_task(unsigned frame, AVFrame *av_frame)
{
	auto &img = video_queue[frame];
	img.pts = video.av_stream ? (av_q2d(video.av_stream->time_base) * double(av_frame->pts)) :
	          double(av_frame->pts) * 1e-6;
	img.sem_to_client.reset();
	assert(img.state == ImageState::Locked);

	if (hw.get_hw_device_type() != AV_HWDEVICE_TYPE_NONE
#ifdef HAVE_FFMPEG_VULKAN
	    && av_frame->format != AV_PIX_FMT_VULKAN
#endif
	    && av_frame->format == hw.get_pix_fmt())
	{
		AVFrame *sw_frame = av_frame_alloc();

		// If we have Vulkan video, we don't need to do anything complicated,
		// but interfacing with any other API is a lot of work.
		if (av_hwframe_transfer_data(sw_frame, av_frame, 0) < 0)
		{
			LOGE("Failed to transfer HW frame.\n");
			av_frame_free(&sw_frame);
			av_frame_free(&av_frame);
		}
		else
		{
			sw_frame->pts = av_frame->pts;
			av_frame_free(&av_frame);
			av_frame = sw_frame;
		}
	}

	bool reset_planes = false;

#ifdef HAVE_FFMPEG_VULKAN
	if (av_frame && av_frame->format == AV_PIX_FMT_VULKAN && video.av_ctx->hw_frames_ctx)
	{
		// If we have Vulkan hwdecode we will bypass the readback + upload stage
		// and go straight to AVVkFrame sharing.
		// hw_frames_ctx is set by the decoder.
		auto *frames = reinterpret_cast<AVHWFramesContext *>(video.av_ctx->hw_frames_ctx->data);

		// As documented, the images in the frame context must be compatible
		// with this SW format. We use the SW format to set up the planes.
		// This is because for e.g. 10-bit we need to know if the bits are
		// written to low order bits or high order bits.
		if (active_upload_pix_fmt != frames->sw_format)
		{
			reset_planes = true;
			active_upload_pix_fmt = frames->sw_format;
		}
	}
	else
#endif
	{
		if (!av_frame || active_upload_pix_fmt != av_frame->format)
		{
			// Not sure if it's possible to just spuriously change the format like this,
			// but be defensive.
			if (av_frame)
				active_upload_pix_fmt = static_cast<AVPixelFormat>(av_frame->format);
			else
				active_upload_pix_fmt = AV_PIX_FMT_NONE;
			reset_planes = true;
		}
	}

	if (reset_planes)
	{
		num_planes = 0;
		// Reset the planar images.
		for (auto &i: video_queue)
			for (auto &plane : i.planes)
				plane.reset();

		// We might not know our target decoding format until this point due to HW decode.
		// Select an appropriate decoding setup.
		if (active_upload_pix_fmt != AV_PIX_FMT_NONE)
			setup_yuv_format_planes();
	}

#ifdef HAVE_FFMPEG_VULKAN
	if (av_frame && av_frame->format == AV_PIX_FMT_VULKAN && video.av_ctx->hw_frames_ctx)
	{
		process_video_frame_in_task_vulkan(img, av_frame, img.sem_to_client);
	}
	else
#endif
	{
		process_video_frame_in_task_upload(img, av_frame, img.sem_to_client);
	}

	if (av_frame)
		av_frame_free(&av_frame);

	// Can now acquire.
	std::lock_guard<std::mutex> holder{lock};
	img.state = ImageState::Ready;
	img.done_ts = Util::get_current_time_nsecs();
	cond.notify_all();
}

void VideoDecoder::Impl::process_video_frame(AVFrame *av_frame)
{
	unsigned frame = acquire_decode_video_frame();

	video_upload_count++;
	video_queue[frame].state = ImageState::Locked;
	video_queue[frame].lock_order = video_upload_count;

	// This decode thread does not have a TLS thread index allocated in the device,
	// only main threads registered as such as well as task group threads satisfy this.
	// Also, we can parallelize video decode and upload + conversion submission,
	// so it's a good idea either way.
	auto task = thread_group->create_task([this, frame, av_frame]() {
		process_video_frame_in_task(frame, av_frame);
	});
	task->set_desc("ffmpeg-decode-upload");
	task->set_task_class(TaskClass::Background);
	task->set_fence_counter_signal(&video_upload_signal);

	// Need to make sure upload tasks are ordered to ensure that frames
	// are acquired in order.
	if (upload_dependency)
		thread_group->add_dependency(*task, *upload_dependency);
	upload_dependency = thread_group->create_task();
	thread_group->add_dependency(*upload_dependency, *task);
}

bool VideoDecoder::Impl::drain_audio_frame()
{
	GRANITE_SCOPED_TIMELINE_EVENT("drain-audio-frame");
#ifdef HAVE_GRANITE_AUDIO
	if (!stream)
		return false;

	// Don't buffer too much. Prefer dropping audio in lieu of massive latency.
	bool drop_high_latency = false;
	if (opts.realtime && float(stream->get_num_buffered_audio_frames()) >
	                     (opts.target_realtime_audio_buffer_time * stream->get_sample_rate()))
		drop_high_latency = true;

	AVFrame *av_frame;
	bool stream_frame;
	if (stream->get_num_buffered_av_frames() <= AVFrameRingStream::FramesHighWatermark && !drop_high_latency)
	{
		// It's okay to acquire the same frame many times.
		av_frame = stream->acquire_write_frame();
		stream_frame = true;
	}
	else
	{
		// This should only happen in real-time mode.
		assert(opts.realtime);

		// Give decoder a dummy frame. We drop audio here.
		av_frame = av_frame_alloc();
		LOGW("Dropping audio frame.\n");
		stream_frame = false;
	}

	int ret;
	if ((ret = avcodec_receive_frame(audio.av_ctx, av_frame)) >= 0)
		if (stream_frame)
			stream->submit_write_frame();

	// This marks the end of the stream. Let it die.
	if (ret == AVERROR_EOF)
		stream->mark_complete();

	if (!stream_frame)
		av_frame_free(&av_frame);

	return ret >= 0;
#else
	return false;
#endif
}

bool VideoDecoder::Impl::decode_audio_packet(AVPacket *pkt)
{
	GRANITE_SCOPED_TIMELINE_EVENT("decode-audio-packet");
#ifdef HAVE_GRANITE_AUDIO
	if (!stream)
		return false;

	int ret;
	if (pkt)
	{
		ret = avcodec_send_packet(audio.av_ctx, pkt);
		if (ret < 0)
		{
			LOGE("Failed to send packet.\n");
			return false;
		}
	}

	return true;
#else
	(void)pkt;
	return false;
#endif
}

bool VideoDecoder::Impl::drain_video_frame()
{
	GRANITE_SCOPED_TIMELINE_EVENT("drain-video-frame");
	AVFrame *frame = av_frame_alloc();
	if (!frame)
		return false;

	if (avcodec_receive_frame(video.av_ctx, frame) >= 0)
	{
		process_video_frame(frame);
		return true;
	}
	else
	{
		av_frame_free(&frame);
		return false;
	}
}

bool VideoDecoder::Impl::decode_video_packet(AVPacket *pkt)
{
	GRANITE_SCOPED_TIMELINE_EVENT("decode-video-packet");
	int ret;
	if (pkt)
	{
		ret = avcodec_send_packet(video.av_ctx, pkt);
		if (ret < 0)
		{
			LOGE("Failed to send packet.\n");
			return false;
		}
	}

	return true;
}

int VideoDecoder::Impl::read_frame(AVPacket *pkt)
{
	GRANITE_SCOPED_TIMELINE_EVENT("read-frame");
	if (av_format_ctx)
	{
		return av_read_frame(av_format_ctx, pkt);
	}
	else if (io_interface)
	{
		av_packet_unref(pkt);

		do
		{
			{
				GRANITE_SCOPED_TIMELINE_EVENT("wait-next-packet");
				if (!io_interface->wait_next_packet())
					return AVERROR_EOF;
			}

			if (av_new_packet(pkt, int(io_interface->get_size())) < 0)
				return AVERROR_EOF;

			memcpy(pkt->data, io_interface->get_data(), pkt->size);
			auto header = io_interface->get_payload_header();
			pkt->pts = header.pts_lo | (int64_t(header.pts_hi) << 32);
			pkt->dts = pkt->pts - header.dts_delta;

			if ((header.encoded & PYRO_PAYLOAD_KEY_FRAME_BIT) != 0)
			{
				av_pkt->flags = AV_PKT_FLAG_KEY;
				has_observed_keyframe = true;
			}
			else
				av_pkt->flags = 0;

			pkt->stream_index = (header.encoded & PYRO_PAYLOAD_STREAM_TYPE_BIT) != 0 ? 1 : 0;
		} while (!has_observed_keyframe);

		return 0;
	}
	else
		return AVERROR_EOF;
}

bool VideoDecoder::Impl::iterate()
{
	std::lock_guard<std::mutex> holder{iteration_lock};

	if (is_video_eof && (is_audio_eof || !audio.av_ctx))
		return false;

	const auto av_pkt_is_video = [this](const AVPacket *pkt)
	{
		int index = video.av_stream ? video.av_stream->index : 0;
		return pkt->stream_index == index;
	};

	const auto av_pkt_is_audio = [this](const AVPacket *pkt)
	{
		if (!audio.av_ctx)
			return false;
		int index = audio.av_stream ? audio.av_stream->index : 1;
		return pkt->stream_index == index;
	};

	if (!is_flushing)
	{
		// When sending a packet, we might not be able to
		// send more packets until we have ensured that
		// all AVFrames have been consumed.
		// If we did something useful in any of these, we've iterated successfully.
		if (drain_video_frame())
			return true;
		if (drain_audio_frame())
			return true;

		int ret;
		if ((ret = read_frame(av_pkt)) >= 0)
		{
			if (av_pkt_is_video(av_pkt))
			{
				if (!decode_video_packet(av_pkt))
					is_video_eof = true;
			}
			else if (av_pkt_is_audio(av_pkt))
			{
				if (!decode_audio_packet(av_pkt))
					is_audio_eof = true;
			}

			av_packet_unref(av_pkt);
		}

		if (ret == AVERROR_EOF)
		{
			// Send a flush packet, so we can drain the codecs.
			// There will be no more packets from the file.
			avcodec_send_packet(video.av_ctx, nullptr);
			if (audio.av_ctx)
				avcodec_send_packet(audio.av_ctx, nullptr);
			is_flushing = true;
		}
		else if (ret < 0)
			return false;
	}

	if (!is_video_eof && is_flushing && !drain_video_frame())
		is_video_eof = true;

	if (!is_audio_eof && is_flushing && audio.av_ctx && !drain_audio_frame())
		is_audio_eof = true;

	return true;
}

bool VideoDecoder::Impl::should_iterate_locked()
{
	// If there are idle images and the audio ring isn't completely saturated, go ahead.
	// The audio ring should be very large to soak variability. Audio does not consume a lot
	// of memory either way.
	// TODO: It is possible to use dynamic rate control techniques to ensure that
	// audio ring does not underflow or overflow.

	// We will never stop decoding, since we have to drain UDP/TDP queues.
	// If player cannot keep up or won't keep up, we drop frames.
	if (opts.realtime)
		return true;

#ifdef HAVE_GRANITE_AUDIO
	if (stream)
	{
		// If audio buffer saturation reached a high watermark, there is risk of overflowing it.
		// We should be far, far ahead at this point. We should easily be able to just sleep
		// until the audio buffer has drained down to a reasonable level.
		if (stream->get_num_buffered_av_frames() > AVFrameRingStream::FramesHighWatermark)
			return false;

		// If audio buffer saturation is at risk of draining, causing audio glitches,
		// we need to catch up.
		// This really shouldn't happen unless application is not actually acquiring images
		// for a good while.
		// When application is in a steady state, it will acquire images based on the audio timestamp.
		// Thus, there is a natural self-regulating mechanism in place.
		// Ensure that we have at least 100 ms of audio buffered up.
		if (mixer->get_stream_state(stream_id) == Audio::Mixer::StreamState::Playing &&
		    stream->get_num_buffered_audio_frames() <= unsigned(audio.av_ctx->sample_rate / 10))
		{
			return true;
		}
	}
#endif

	// If audio is in a stable situation, we can shift our attention to video.
	// Video is more lenient w.r.t. drops and such.

	// If acquire is blocking despite us having no idle images,
	// it means it's not happy with whatever frames we have decoded,
	// so we should go ahead, even if it means trampling on existing frames.
	if (acquire_blocking)
		return true;

	// We're in a happy state where we only desire progress if there is anything
	// meaningful to do.
	return find_idle_decode_video_frame_locked() >= 0;
}

void VideoDecoder::Impl::thread_main()
{
	Util::set_current_thread_priority(Util::ThreadPriority::High);
	Util::set_current_thread_name("ffmpeg-decode");
	Util::TimelineTraceFile::set_tid("ffmpeg-decode");
	Global::set_thread_context(*managers);
	if (auto *tg = GRANITE_THREAD_GROUP())
		tg->refresh_global_timeline_trace_file();

	for (;;)
	{
		{
			std::unique_lock<std::mutex> holder{lock};

			while (!should_iterate_locked() && !teardown)
			{
#ifdef HAVE_GRANITE_AUDIO
				// If we're going to sleep, we need to make sure we don't sleep for so long that we drain the audio queue.
				if (stream && mixer->get_stream_state(stream_id) == Audio::Mixer::StreamState::Playing)
				{
					// We want to sleep until there is ~100ms audio left.
					// Need a decent amount of headroom since we might have to decode video before
					// we can pump more audio frames.
					// This could be improved with dedicated decoding threads audio and video,
					// but that is a bit overkill.
					// Reformulate the expression to avoid potential u32 overflow if multiplying.
					// Shouldn't need floats here.
					int sleep_ms = int(stream->get_num_buffered_audio_frames() / ((audio.av_ctx->sample_rate + 999) / 1000));
					sleep_ms = std::max<int>(sleep_ms - 100 + 5, 0);
					cond.wait_for(holder, std::chrono::milliseconds(sleep_ms));
				}
				else
#endif
				{
					cond.wait(holder);
				}
			}
		}

		if (teardown)
			break;

		if (!iterate())
		{
			// Ensure acquire thread can observe last frame if it observes
			// the acquire_is_eof flag.
			video_upload_signal.wait_until_at_least(video_upload_count);

			std::lock_guard<std::mutex> holder{lock};
			teardown = true;
			acquire_is_eof = true;
			cond.notify_one();
			break;
		}
	}
}

bool VideoDecoder::Impl::is_eof()
{
	if (!decode_thread.joinable())
		return true;

	std::unique_lock<std::mutex> holder{lock};
	return acquire_is_eof;
}

int VideoDecoder::Impl::try_acquire_video_frame(VideoFrame &frame)
{
	if (!decode_thread.joinable())
		return false;

	std::unique_lock<std::mutex> holder{lock};

	int index = find_acquire_video_frame_locked();

	if (index >= 0)
	{
		// Now we can return a frame.
		frame.sem.reset();
		std::swap(frame.sem, video_queue[index].sem_to_client);
		video_queue[index].state = ImageState::Acquired;
		frame.view = &video_queue[index].rgb_image->get_view();
		frame.index = index;
		frame.pts = video_queue[index].pts;
		frame.done_ts = video_queue[index].done_ts;

		// Progress.
		cond.notify_one();

		return 1;
	}
	else
	{
		return acquire_is_eof || teardown ? -1 : 0;
	}
}

bool VideoDecoder::Impl::acquire_video_frame(VideoFrame &frame, int timeout_ms)
{
	if (!decode_thread.joinable())
		return false;

	std::unique_lock<std::mutex> holder{lock};

	// Wake up decode thread to make sure it knows acquire thread
	// is blocking and awaits forward progress.
	acquire_blocking = true;
	cond.notify_one();

	int index = -1;

	// Poll the video queue for new frames.
	if (timeout_ms >= 0)
	{
		auto target_time = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
		bool success = cond.wait_until(holder, target_time, [this, &index]() {
			return (index = find_acquire_video_frame_locked()) >= 0 || acquire_is_eof || teardown;
		});
		if (!success)
			return false;
	}
	else
	{
		cond.wait(holder, [this, &index]() {
			return (index = find_acquire_video_frame_locked()) >= 0 || acquire_is_eof || teardown;
		});
	}

	if (index < 0)
		return false;

	// Now we can return a frame.
	frame.sem.reset();
	std::swap(frame.sem, video_queue[index].sem_to_client);
	video_queue[index].state = ImageState::Acquired;
	frame.view = &video_queue[index].rgb_image->get_view();
	frame.index = index;
	frame.pts = video_queue[index].pts;
	frame.done_ts = video_queue[index].done_ts;

	// Progress.
	acquire_blocking = false;
	cond.notify_one();
	return true;
}

void VideoDecoder::Impl::release_video_frame(unsigned index, Vulkan::Semaphore sem)
{
	// Need to hold lock here.
	assert(video_queue[index].state == ImageState::Acquired);
	video_queue[index].state = ImageState::Idle;
	video_queue[index].sem_from_client = std::move(sem);
	video_queue[index].idle_order = ++idle_timestamps;
}

bool VideoDecoder::Impl::begin_device_context(Vulkan::Device *device_, const FFmpegDecode::Shaders<> &shaders_)
{
	device = device_;
	shaders = shaders_;
	thread_group = device->get_system_handles().thread_group;

	// Potentially need device here if we're creating a Vulkan HW context.
	if (!init_video_decoder_post_device())
		return false;

	return true;
}

double VideoDecoder::Impl::get_estimated_audio_playback_timestamp_raw()
{
#ifdef HAVE_GRANITE_AUDIO
	if (stream)
	{
		uint32_t pts_buffer_index = (stream->pts_index.load(std::memory_order_acquire) - 1) %
		                            AVFrameRingStream::Frames;

		double pts = stream->progress[pts_buffer_index].pts;
		if (pts < 0.0)
		{
			pts = 0.0;
		}
		else if (!is_paused)
		{
			// Crude estimate based on last reported PTS, offset by time since reported.
			int64_t sampled_ns = stream->progress[pts_buffer_index].sampled_ns;
			int64_t d = std::max<int64_t>(Util::get_current_time_nsecs(), sampled_ns) - sampled_ns;
			pts += 1e-9 * double(d);
		}

		return pts;
	}
	else
#endif
	{
		return -1.0;
	}
}

double VideoDecoder::Impl::get_audio_buffering_duration()
{
#ifdef HAVE_GRANITE_AUDIO
	if (stream)
		return double(stream->get_num_buffered_audio_frames()) / stream->sample_rate;
	else
		return -1.0;
#else
	return -1.0;
#endif
}

void VideoDecoder::Impl::set_audio_delta_rate_factor(float delta)
{
#ifdef HAVE_GRANITE_AUDIO
	if (delta > 0.10f)
	{
		// Speed up, audio buffer is too large.
		stream->set_rate_factor(1.005f);
	}
	else if (delta < -0.10f)
	{
		// Slow down.
		stream->set_rate_factor(0.995f);
	}
	else
	{
		// This is inaudible in practice. Practical distortion will be much lower than outer limits.
		// And should be less than 1 cent on average.
		stream->set_rate_factor(1.0f + delta * 0.05f);
	}
#else
	(void)delta;
#endif
}

void VideoDecoder::Impl::latch_estimated_audio_playback_timestamp(double pts)
{
#ifdef HAVE_GRANITE_AUDIO
	if (!stream)
		return;

	auto delta = float(pts - get_estimated_audio_playback_timestamp_raw());
	set_audio_delta_rate_factor(delta);
#else
	(void)pts;
#endif
}

void VideoDecoder::Impl::latch_audio_buffering_target(double target_buffer_time)
{
#ifdef HAVE_GRANITE_AUDIO
	if (!stream)
		return;

	double current_time = get_audio_buffering_duration();
	auto delta = float(current_time - target_buffer_time);
	set_audio_delta_rate_factor(delta);
#else
	(void)target_buffer_time;
#endif
}

double VideoDecoder::Impl::get_last_video_buffering_pts()
{
	double last_pts = -1.0;
	std::unique_lock<std::mutex> holder{lock};
	for (auto &q : video_queue)
		if (q.state == ImageState::Ready || q.state == ImageState::Acquired)
			if (q.pts > last_pts)
				last_pts = q.pts;
	return last_pts;
}

unsigned VideoDecoder::Impl::get_num_ready_video_frames()
{
	std::unique_lock<std::mutex> holder{lock};
	unsigned count = 0;
	for (auto &q : video_queue)
		if (q.state == ImageState::Ready)
			count++;
	return count;
}

double VideoDecoder::Impl::latch_estimated_video_playback_timestamp(double elapsed_time, double target_latency)
{
	if (smooth_elapsed == 0.0)
	{
		smooth_elapsed = elapsed_time;
		smooth_pts = get_last_video_buffering_pts() - target_latency;
		if (smooth_pts < 0.0)
			smooth_pts = 0.0;
	}
	else
	{
		double target_pts = get_last_video_buffering_pts() - target_latency;
		if (target_pts < 0.0)
			target_pts = 0.0;

		// This is the value we should get in principle if everything is steady.
		smooth_pts += elapsed_time - smooth_elapsed;
		smooth_elapsed = elapsed_time;

		if (muglm::abs(smooth_pts - target_pts) > 0.25)
		{
			// Massive spike somewhere, cannot smooth.
			// Reset the PTS.
			smooth_elapsed = elapsed_time;
			smooth_pts = target_pts;
		}
		else
		{
			// Bias slightly towards the true estimated PTS.
			smooth_pts += 0.002 * (target_pts - smooth_pts);
		}
	}

	latch_estimated_audio_playback_timestamp(smooth_pts);
	return smooth_pts;
}

double VideoDecoder::Impl::get_estimated_audio_playback_timestamp(double elapsed_time)
{
#ifdef HAVE_GRANITE_AUDIO
	if (stream)
	{
		// Unsmoothed PTS.
		auto pts = get_estimated_audio_playback_timestamp_raw();

		if (pts == 0.0 || smooth_elapsed == 0.0)
		{
			// Latch the PTS.
			smooth_elapsed = elapsed_time;
			smooth_pts = pts;
		}
		else
		{
			// Smooth out the reported PTS.
			// The reported PTS should be tied to the host timer,
			// but we need to gradually adjust the timer based on the reported audio PTS to be accurate over time.

			// This is the value we should get in principle if everything is steady.
			smooth_pts += elapsed_time - smooth_elapsed;
			smooth_elapsed = elapsed_time;

			if (muglm::abs(smooth_pts - pts) > 0.25)
			{
				// Massive spike somewhere, cannot smooth.
				// Reset the PTS.
				smooth_elapsed = elapsed_time;
				smooth_pts = pts;
			}
			else
			{
				// Bias slightly towards the true estimated PTS.
				smooth_pts += 0.005 * (pts - smooth_pts);
			}
		}

		return smooth_pts;
	}
	else
#endif
	{
		(void)elapsed_time;
		return -1.0;
	}
}

void VideoDecoder::Impl::flush_codecs()
{
	for (auto &img : video_queue)
	{
		img.rgb_image.reset();
		img.rgb_storage_view.reset();
		for (auto &plane : img.planes)
			plane.reset();
		img.sem_to_client.reset();
		img.sem_from_client.reset();
		img.idle_order = 0;
		img.lock_order = 0;
		img.state = ImageState::Idle;
		img.pts = 0.0;
		img.done_ts = 0;
	}

	if (video.av_ctx)
		avcodec_flush_buffers(video.av_ctx);
	if (audio.av_ctx)
		avcodec_flush_buffers(audio.av_ctx);

#ifdef HAVE_GRANITE_AUDIO
	if (stream)
	{
		mixer->kill_stream(stream_id);
		stream->release_reference();
		stream = nullptr;
	}
#endif
}

void VideoDecoder::Impl::end_device_context()
{
	stop();

	free_av_objects(video);
	free_av_objects(audio);

	if (av_format_ctx)
		avformat_close_input(&av_format_ctx);
	if (av_pkt)
		av_packet_free(&av_pkt);

	hw.reset();
	device = nullptr;
	thread_group = nullptr;
}

bool VideoDecoder::Impl::play()
{
	if (!device)
		return false;
	if (decode_thread.joinable())
		return false;

	teardown = false;
	begin_audio_stream();

	decode_thread = std::thread(&Impl::thread_main, this);
	return true;
}

bool VideoDecoder::Impl::get_stream_id(Audio::StreamID &id) const
{
#ifdef HAVE_GRANITE_AUDIO
	id = stream_id;
	return bool(id);
#else
	(void)id;
	return false;
#endif
}

bool VideoDecoder::Impl::stop()
{
	if (!decode_thread.joinable())
		return false;

	{
		std::lock_guard<std::mutex> holder{lock};
		teardown = true;
		cond.notify_one();
	}
	decode_thread.join();
	video_upload_signal.wait_until_at_least(video_upload_count);
	upload_dependency.reset();
	flush_codecs();
	return true;
}

bool VideoDecoder::Impl::get_paused() const
{
	return is_paused;
}

void VideoDecoder::Impl::set_paused(bool enable)
{
	is_paused = enable;
#ifdef HAVE_GRANITE_AUDIO
	if (stream)
	{
		// Reset PTS smoothing.
		smooth_elapsed = 0.0;
		smooth_pts = 0.0;

		bool result;
		if (enable)
			result = mixer->pause_stream(stream_id);
		else
		{
			// When we uncork, we need to ensure that estimated PTS
			// picks off where we expect.
			stream->mark_uncorked_audio_pts();

			// If the thread went to deep sleep, we need to make sure it knows
			// about the stream state being playing.
			std::lock_guard<std::mutex> holder{lock};
			result = mixer->play_stream(stream_id);
			cond.notify_one();
		}

		if (!result)
			LOGE("Failed to set stream state.\n");
	}
#endif
}

bool VideoDecoder::Impl::seek(double ts)
{
	if (!av_format_ctx)
		return false;

	std::lock_guard<std::mutex> holder{iteration_lock};

	// Drain this before we take the global lock, since a video task needs to take the global lock to update state.
	video_upload_signal.wait_until_at_least(video_upload_count);

	std::lock_guard<std::mutex> holder2{lock};
	cond.notify_one();

	if (ts < 0.0)
		ts = 0.0;
	auto target_ts = int64_t(AV_TIME_BASE * ts);

	if (avformat_seek_file(av_format_ctx, -1, INT64_MIN, target_ts, INT64_MAX, 0) < 0)
	{
		LOGE("Failed to seek file.\n");
		return false;
	}

	if (decode_thread.joinable())
	{
		flush_codecs();
		begin_audio_stream();
		return true;
	}
	else
		return play();
}

VideoDecoder::Impl::~Impl()
{
	end_device_context();
}

VideoDecoder::VideoDecoder()
{
	impl.reset(new Impl);
}

VideoDecoder::~VideoDecoder()
{
}

bool VideoDecoder::init(Audio::Mixer *mixer, const char *path, const DecodeOptions &opts)
{
	return impl->init(mixer, path, opts);
}

void VideoDecoder::set_io_interface(Granite::DemuxerIOInterface *iface)
{
	impl->io_interface = iface;
}

unsigned VideoDecoder::get_width() const
{
	return impl->get_width();
}

unsigned VideoDecoder::get_height() const
{
	return impl->get_height();
}

bool VideoDecoder::begin_device_context(Vulkan::Device *device, const FFmpegDecode::Shaders<> &shaders)
{
	return impl->begin_device_context(device, shaders);
}

void VideoDecoder::end_device_context()
{
	impl->end_device_context();
}

bool VideoDecoder::play()
{
	return impl->play();
}

bool VideoDecoder::get_stream_id(Audio::StreamID &id) const
{
	return impl->get_stream_id(id);
}

bool VideoDecoder::stop()
{
	return impl->stop();
}

bool VideoDecoder::seek(double ts)
{
	return impl->seek(ts);
}

void VideoDecoder::set_paused(bool state)
{
	return impl->set_paused(state);
}

bool VideoDecoder::get_paused() const
{
	return impl->get_paused();
}

double VideoDecoder::get_estimated_audio_playback_timestamp(double elapsed_time)
{
	return impl->get_estimated_audio_playback_timestamp(elapsed_time);
}

double VideoDecoder::latch_estimated_video_playback_timestamp(double elapsed_time, double target_latency)
{
	return impl->latch_estimated_video_playback_timestamp(elapsed_time, target_latency);
}

double VideoDecoder::get_audio_buffering_duration()
{
	return impl->get_audio_buffering_duration();
}

double VideoDecoder::get_last_video_buffering_pts()
{
	return impl->get_last_video_buffering_pts();
}

unsigned int VideoDecoder::get_num_ready_video_frames()
{
	return impl->get_num_ready_video_frames();
}

double VideoDecoder::get_estimated_audio_playback_timestamp_raw()
{
	return impl->get_estimated_audio_playback_timestamp_raw();
}

bool VideoDecoder::acquire_video_frame(VideoFrame &frame, int timeout_ms)
{
	return impl->acquire_video_frame(frame, timeout_ms);
}

int VideoDecoder::try_acquire_video_frame(VideoFrame &frame)
{
	return impl->try_acquire_video_frame(frame);
}

bool VideoDecoder::is_eof()
{
	return impl->is_eof();
}

void VideoDecoder::release_video_frame(unsigned index, Vulkan::Semaphore sem)
{
	impl->release_video_frame(index, std::move(sem));
}

void VideoDecoder::latch_audio_buffering_target(double buffer_time)
{
	impl->latch_audio_buffering_target(buffer_time);
}

float VideoDecoder::get_audio_sample_rate() const
{
#ifdef HAVE_GRANITE_AUDIO
	if (impl->audio.av_ctx)
		return float(impl->audio.av_ctx->sample_rate);
	else
		return -1.0f;
#else
	return -1.0f;
#endif
}

uint32_t VideoDecoder::get_audio_underflow_counter() const
{
#ifdef HAVE_GRANITE_AUDIO
	if (impl->stream)
		return impl->stream->get_underflow_counter();
	else
		return 0;
#else
	return 0;
#endif
}
}
