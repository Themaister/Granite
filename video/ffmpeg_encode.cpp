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

#include "ffmpeg_encode.hpp"
#include "ffmpeg_hw_device.hpp"
#include "logging.hpp"
#include "math.hpp"
#include "muglm/muglm_impl.hpp"
#include "transforms.hpp"
#include "thread_group.hpp"
#include "timer.hpp"
#include "dynamic_array.hpp"
#include <thread>
#include <cstdlib>
#include "pyroenc.hpp"
#ifdef HAVE_GRANITE_AUDIO
#include "audio_interface.hpp"
#include "dsp/dsp.hpp"
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
	AVStream *av_stream_local = nullptr;
	AVFrame *av_frame = nullptr;
	AVCodecContext *av_ctx = nullptr;
	AVPacket *av_pkt = nullptr;
	int ticks_per_frame = 1;
};

struct VideoEncoder::YCbCrPipelineData
{
	Vulkan::ImageHandle luma;
	Vulkan::ImageHandle chroma_full;
	Vulkan::ImageHandle chroma;
	Vulkan::BufferHandle buffer;
	Vulkan::Fence fence;
	Vulkan::Program *rgb_to_ycbcr = nullptr;
	Vulkan::Program *chroma_downsample = nullptr;
	Vulkan::Program *rgb_scale = nullptr;
	PlaneLayout planes[3] = {};
	unsigned num_planes = 0;
	bool pyroenc = false;

	struct Constants
	{
		float inv_resolution_luma[2];
		float inv_resolution_chroma[2];
		float base_uv_luma[2];
		float base_uv_chroma[2];
		uint32_t luma_dispatch[2];
		uint32_t chroma_dispatch[2];
		float dither_strength;
	} constants = {};

	AVFrame *hw_frame = nullptr;
	~YCbCrPipelineData()
	{
		if (hw_frame)
			av_frame_free(&hw_frame);
	}
};

void VideoEncoder::YCbCrPipelineDataDeleter::operator()(YCbCrPipelineData *ptr)
{
	delete ptr;
}

struct VideoEncoder::Impl final
#ifdef HAVE_GRANITE_AUDIO
		: public Audio::RecordCallback
#endif
{
	Vulkan::Device *device = nullptr;
	bool init(Vulkan::Device *device, const char *path, const Options &options);
	void set_mux_stream_callback(MuxStreamCallback *callback);
	bool encode_frame(const uint8_t *buffer, const PlaneLayout *planes, unsigned num_planes,
	                  int64_t pts, int compensate_audio_us);
	bool encode_frame(AVFrame *hw_frame, int64_t pts, int compensate_audio_us);
	bool encode_frame(const Vulkan::ImageView &view, int64_t pts, int compensate_audio_us);
	~Impl();

	AVFormatContext *av_format_ctx = nullptr;
	AVFormatContext *av_format_ctx_local = nullptr;
	CodecStream video, audio;
	Options options;
	Audio::DumpBackend *audio_source = nullptr;
	Audio::RecordStream *audio_stream = nullptr;
	MuxStreamCallback *mux_stream_callback = nullptr;

	void drain_codec();
	AVFrame *alloc_video_frame(AVPixelFormat pix_fmt, unsigned width, unsigned height);

	AVFrame *alloc_audio_frame(AVSampleFormat samp_format, AVChannelLayout channel_layout,
	                           unsigned sample_rate, unsigned sample_count);

	bool drain_packets(CodecStream &stream);

	bool init_video_codec_av(const AVCodec *codec);
	bool init_video_codec_pyro(PyroEnc::Profile profile);
	bool init_video_codec();
	bool init_audio_codec();

	std::vector<int16_t> audio_buffer_s16;

	struct
	{
		int64_t next_lower_bound_pts = 0;
		int64_t next_upper_bound_pts = 0;
		int64_t base_pts = 0;
	} realtime_pts;
	int64_t encode_video_pts = 0;
	int64_t encode_audio_pts = 0;
	int64_t audio_pts = 0;
	int current_audio_frames = 0;

	FFmpegHWDevice hw;

	int64_t sample_realtime_pts() const;
	std::atomic_int audio_compensate_us;

#ifdef HAVE_GRANITE_AUDIO
	bool encode_audio();
	bool encode_audio_stream(const float *data, size_t frames, int compensate_audio_us);
	bool encode_audio_source();
	void write_frames_interleaved_f32(const float *data, size_t frames) override;
#endif

#ifdef HAVE_FFMPEG_VULKAN
	void submit_process_rgb_vulkan(Vulkan::CommandBufferHandle &cmd, YCbCrPipelineData &pipeline);
#endif
	void submit_process_rgb_readback(Vulkan::CommandBufferHandle &cmd, YCbCrPipelineData &pipeline);
	void submit_process_rgb_pyro(Vulkan::CommandBufferHandle &cmd, YCbCrPipelineData &pipeline);

	pyro_codec_parameters pyro_codec = {};
	std::mutex mux_lock;
	PyroEnc::Encoder pyro_encoder;
	bool using_pyro_encoder = false;
};

static void free_av_objects(CodecStream &stream)
{
	if (stream.av_frame)
		av_frame_free(&stream.av_frame);
	if (stream.av_pkt)
		av_packet_free(&stream.av_pkt);
	if (stream.av_ctx)
		avcodec_free_context(&stream.av_ctx);
}

void VideoEncoder::Impl::drain_codec()
{
	if (av_format_ctx)
	{
		if (video.av_pkt)
		{
			int ret = avcodec_send_frame(video.av_ctx, nullptr);
			if (ret < 0)
				LOGE("Failed to send packet to codec: %d\n", ret);
			else if (!drain_packets(video))
				LOGE("Failed to drain codec of packets.\n");
		}

		if (audio.av_pkt)
		{
			int ret = avcodec_send_frame(audio.av_ctx, nullptr);
			if (ret < 0)
				LOGE("Failed to send packet to codec: %d\n", ret);
			else if (!drain_packets(audio))
				LOGE("Failed to drain codec of packets.\n");
		}

		const auto close_fmt_context = [this](AVFormatContext *fmt_ctx) {
			if (fmt_ctx)
			{
				av_write_trailer(fmt_ctx);
				if (!(fmt_ctx->flags & AVFMT_NOFILE))
				{
					if (fmt_ctx == av_format_ctx && mux_stream_callback)
					{
						av_freep(&fmt_ctx->pb->buffer);
						avio_context_free(&fmt_ctx->pb);
					}
					else
						avio_closep(&fmt_ctx->pb);
				}
				avformat_free_context(fmt_ctx);
			}
		};

		close_fmt_context(av_format_ctx);
		close_fmt_context(av_format_ctx_local);
	}

	free_av_objects(video);
	free_av_objects(audio);
}

void VideoEncoder::Impl::set_mux_stream_callback(Granite::MuxStreamCallback *callback)
{
	mux_stream_callback = callback;
}

VideoEncoder::Impl::~Impl()
{
	drain_codec();
	hw.reset();
}

static unsigned format_to_planes(VideoEncoder::Format fmt)
{
	switch (fmt)
	{
	case VideoEncoder::Format::NV12:
	case VideoEncoder::Format::P016:
	case VideoEncoder::Format::P010:
		return 2;

	default:
		return 0;
	}
}

int64_t VideoEncoder::Impl::sample_realtime_pts() const
{
	return int64_t(Util::get_current_time_nsecs() / 1000) - realtime_pts.base_pts;
}

#ifdef HAVE_GRANITE_AUDIO
bool VideoEncoder::Impl::encode_audio_source()
{
	// Render out audio in the main thread to ensure exact reproducibility across runs.
	// If we don't care about that, we can render audio directly in the thread worker.
	int64_t target_audio_samples = av_rescale_q_rnd(encode_video_pts, video.av_ctx->time_base, audio.av_ctx->time_base, AV_ROUND_UP);
	int64_t to_render = std::max<int64_t>(target_audio_samples - audio_pts, 0);
	audio_buffer_s16.resize(to_render * 2);
	audio_source->drain_interleaved_s16(audio_buffer_s16.data(), to_render);
	audio_pts += to_render;
	int ret;

	if (audio.av_pkt)
	{
		for (size_t i = 0, n = audio_buffer_s16.size() / 2; i < n; )
		{
			int to_copy = std::min<int>(int(n - i), audio.av_frame->nb_samples - current_audio_frames);

			if (current_audio_frames == 0)
			{
				if ((ret = av_frame_make_writable(audio.av_frame)) < 0)
				{
					LOGE("Failed to make frame writable: %d.\n", ret);
					return false;
				}
			}

			memcpy(reinterpret_cast<int16_t *>(audio.av_frame->data[0]) + 2 * current_audio_frames,
			       audio_buffer_s16.data() + 2 * i, to_copy * 2 * sizeof(int16_t));

			current_audio_frames += to_copy;

			if (current_audio_frames == audio.av_frame->nb_samples)
			{
				audio.av_frame->pts = encode_audio_pts;
				encode_audio_pts += current_audio_frames;
				current_audio_frames = 0;

				ret = avcodec_send_frame(audio.av_ctx, audio.av_frame);
				if (ret < 0)
				{
					LOGE("Failed to send packet to codec: %d\n", ret);
					return false;
				}

				if (!drain_packets(audio))
				{
					LOGE("Failed to drain audio packets.\n");
					return false;
				}
			}

			i += to_copy;
		}
	}

	return true;
}

bool VideoEncoder::Impl::encode_audio_stream(const float *data, size_t frames, int compensate_audio_us)
{
	constexpr int channels = 2;
	int ret;

	while (frames)
	{
		if (current_audio_frames == 0)
		{
			if ((ret = av_frame_make_writable(audio.av_frame)) < 0)
			{
				LOGE("Failed to make frame writable: %d.\n", ret);
				return false;
			}
		}
		int to_write = std::min<int>(int(frames), audio.av_frame->nb_samples - current_audio_frames);

		if (audio.av_frame->format == AV_SAMPLE_FMT_FLT)
		{
			memcpy(audio.av_frame->data[0] + current_audio_frames * sizeof(float) * channels,
			       data, to_write * sizeof(float) * channels);
		}
		else if (audio.av_frame->format == AV_SAMPLE_FMT_FLTP)
		{
			Audio::DSP::deinterleave_stereo_f32(
					reinterpret_cast<float *>(audio.av_frame->data[0]) + current_audio_frames,
					reinterpret_cast<float *>(audio.av_frame->data[1]) + current_audio_frames,
					data, to_write);
		}
		else
		{
			Audio::DSP::f32_to_i16(reinterpret_cast<int16_t *>(audio.av_frame->data[0]) + current_audio_frames * 2,
			                       data, to_write * 2);
		}

		current_audio_frames += to_write;
		frames -= to_write;
		data += to_write * channels;

		if (current_audio_frames == audio.av_frame->nb_samples)
		{
			// Crude system for handling drift.
			// Ensure monotonic PTS with maximum 1% clock drift.
			auto absolute_ts = sample_realtime_pts() + compensate_audio_us;

			// Detect large discontinuity and reset the PTS.
			absolute_ts = std::max(absolute_ts, realtime_pts.next_lower_bound_pts);
			if (absolute_ts < realtime_pts.next_upper_bound_pts + 200000)
				absolute_ts = std::min(absolute_ts, realtime_pts.next_upper_bound_pts);

			audio.av_frame->pts = absolute_ts;
			realtime_pts.next_lower_bound_pts =
					absolute_ts + av_rescale_rnd(audio.av_frame->nb_samples, 990000, audio.av_ctx->sample_rate, AV_ROUND_DOWN);
			realtime_pts.next_upper_bound_pts =
					absolute_ts + av_rescale_rnd(audio.av_frame->nb_samples, 1010000, audio.av_ctx->sample_rate, AV_ROUND_UP);

			ret = avcodec_send_frame(audio.av_ctx, audio.av_frame);
			if (ret < 0)
			{
				LOGE("Failed to send packet to codec: %d\n", ret);
				return false;
			}

			if (!drain_packets(audio))
			{
				LOGE("Failed to drain audio packets.\n");
				return false;
			}

			current_audio_frames = 0;
		}
	}

	return true;
}

bool VideoEncoder::Impl::encode_audio()
{
	// For realtime, we'll pump from a record callback.
	if (!options.realtime && audio_source)
		return encode_audio_source();
	else
		return true;
}

void VideoEncoder::Impl::write_frames_interleaved_f32(const float *data, size_t frames)
{
	if (mux_stream_callback && pyro_codec.audio_codec == PYRO_AUDIO_CODEC_RAW_S16LE)
	{
		audio_buffer_s16.resize(frames * pyro_codec.channels);
		if (data)
			Audio::DSP::f32_to_i16(audio_buffer_s16.data(), data, frames * pyro_codec.channels);
		else
			memset(audio_buffer_s16.data(), 0, frames * pyro_codec.channels);

		// In this mode, we don't care about smoothing the PTS at all or compensating for latency.
		int64_t pts = sample_realtime_pts();
		mux_stream_callback->write_audio_packet(
				pts, pts, audio_buffer_s16.data(), audio_buffer_s16.size() * sizeof(int16_t));
	}
	else
	{
		encode_audio_stream(data, frames, audio_compensate_us.load(std::memory_order_relaxed));
	}
}
#endif

bool VideoEncoder::Impl::encode_frame(const Vulkan::ImageView &view, int64_t pts, int compensate_audio_us)
{
	assert(mux_stream_callback);

	PyroEnc::FrameInfo frame = {};
	frame.pts = pts;
	frame.force_idr = mux_stream_callback->should_force_idr();
	frame.width = view.get_view_width();
	frame.height = view.get_view_height();
	frame.view = view.get_view();

	device->external_queue_lock();
	auto send_result = pyro_encoder.send_frame(frame);
	device->external_queue_unlock();
	if (send_result != PyroEnc::Result::Success)
		return false;

	PyroEnc::EncodedFrame encoded_frame;
	while (pyro_encoder.receive_encoded_frame(encoded_frame) == PyroEnc::Result::Success)
	{
		// Encode should happen in a threaded task, so blocking here is fine.
		if (!encoded_frame.wait())
		{
			LOGE("Failed to wait for packet.\n");
			return false;
		}

		if (encoded_frame.is_idr())
		{
			size_t combined_size = encoded_frame.get_size() + pyro_encoder.get_encoded_parameters_size();
			std::unique_ptr<uint8_t[]> buf{new uint8_t[combined_size]};
			memcpy(buf.get(), pyro_encoder.get_encoded_parameters(),
			       pyro_encoder.get_encoded_parameters_size());
			memcpy(buf.get() + pyro_encoder.get_encoded_parameters_size(),
			       encoded_frame.get_payload(), encoded_frame.get_size());
			mux_stream_callback->write_video_packet(
					encoded_frame.get_pts(), encoded_frame.get_dts(),
					buf.get(), combined_size,
					true);
		}
		else
		{
			mux_stream_callback->write_video_packet(
					encoded_frame.get_pts(), encoded_frame.get_dts(),
					encoded_frame.get_payload(), encoded_frame.get_size(),
					false);
		}
	}

	audio_compensate_us.store(compensate_audio_us, std::memory_order_relaxed);

#ifdef HAVE_GRANITE_AUDIO
	if (!encode_audio())
	{
		LOGE("Failed to encode audio.\n");
		return false;
	}
#endif

	return true;
}

bool VideoEncoder::Impl::encode_frame(AVFrame *hw_frame, int64_t pts, int compensate_audio_us)
{
	if (options.realtime)
		hw_frame->pts = pts;
	else
		hw_frame->pts = encode_video_pts++;

	int ret = avcodec_send_frame(video.av_ctx, hw_frame);
	if (ret < 0)
	{
		LOGE("Failed to send packet to codec: %d\n", ret);
		return false;
	}

	if (!drain_packets(video))
	{
		LOGE("Failed to drain video packets.\n");
		return false;
	}

	audio_compensate_us.store(compensate_audio_us, std::memory_order_relaxed);

#ifdef HAVE_GRANITE_AUDIO
	if (!encode_audio())
	{
		LOGE("Failed to encode audio.\n");
		return false;
	}
#endif

	return true;
}

bool VideoEncoder::Impl::encode_frame(const uint8_t *buffer, const PlaneLayout *planes, unsigned num_planes,
                                      int64_t pts, int compensate_audio_us)
{
	if (num_planes != format_to_planes(options.format))
	{
		LOGE("Invalid number of planes.\n");
		return false;
	}

	int ret;

	if ((ret = av_frame_make_writable(video.av_frame)) < 0)
	{
		LOGE("Failed to make frame writable: %d.\n", ret);
		return false;
	}

	// Feels a bit dumb to use swscale just to copy.
	// Ideally we'd be able to set the data pointers directly in AVFrame,
	// but encoder reference buffers probably require a copy anyways ...
	unsigned pix_size = options.format != VideoEncoder::Format::NV12 ? 2 : 1;
	const auto *src_luma = buffer + planes[0].offset;
	const auto *src_chroma = buffer + planes[1].offset;
	auto *dst_luma = video.av_frame->data[0];
	auto *dst_chroma = video.av_frame->data[1];

	unsigned chroma_width = (options.width >> 1) * 2;
	unsigned chroma_height = options.height >> 1;
	for (unsigned y = 0; y < options.height; y++, dst_luma += video.av_frame->linesize[0], src_luma += planes[0].stride)
		memcpy(dst_luma, src_luma, options.width * pix_size);
	for (unsigned y = 0; y < chroma_height; y++, dst_chroma += video.av_frame->linesize[1], src_chroma += planes[1].stride)
		memcpy(dst_chroma, src_chroma, chroma_width * pix_size);

	video.av_frame->pict_type = AV_PICTURE_TYPE_NONE;

	if (mux_stream_callback && options.low_latency)
	{
		// Ensure monotonic PTS, or codecs blow up.
		if (pts <= encode_video_pts)
			pts = encode_video_pts + 1;

		video.av_frame->pts = pts;
		encode_video_pts = pts;
	}
	else if (options.realtime)
	{
		int64_t target_pts = av_rescale_q_rnd(pts, AV_TIME_BASE_Q, video.av_ctx->time_base, AV_ROUND_ZERO);

		if (encode_video_pts)
		{
			int64_t delta = std::abs(target_pts - encode_video_pts);
			if (delta > 8 * video.ticks_per_frame)
			{
				// If we're way off (8 frames), catch up instantly.
				encode_video_pts = target_pts;

				// Force an I-frame here since there is a large discontinuity.
				video.av_frame->pict_type = AV_PICTURE_TYPE_I;
			}
			else if (delta >= video.ticks_per_frame / 4)
			{
				// If we're more than a quarter frame off, nudge the PTS by one subtick to catch up with real value.
				// Nudging slowly avoids broken DTS timestamps.
				encode_video_pts += target_pts > encode_video_pts ? 1 : -1;
			}
		}
		else
		{
			// First frame is latched.
			encode_video_pts = target_pts;
		}

		// Try to remain a steady PTS, adjust as necessary to account for drift and drops.
		// This helps avoid DTS issues in misc hardware encoders since they treat DTS as just subtracted reordered PTS,
		// or something weird like that ...
		video.av_frame->pts = encode_video_pts;
		encode_video_pts += video.ticks_per_frame;
	}
	else
		video.av_frame->pts = encode_video_pts++;

	// When new stream clients come in we need to force IDR frames.
	// This is not necessary for x264 apparently, but NVENC does ...
	// Callback is responsible for controlling how often an IDR should be sent.
	if (mux_stream_callback && mux_stream_callback->should_force_idr())
		video.av_frame->pict_type = AV_PICTURE_TYPE_I;

	AVFrame *hw_frame = nullptr;
	if (hw.get_hw_device_type() != AV_HWDEVICE_TYPE_NONE)
	{
		hw_frame = av_frame_alloc();
		if (av_hwframe_get_buffer(video.av_ctx->hw_frames_ctx, hw_frame, 0) < 0)
		{
			LOGE("Failed to get HW buffer.\n");
			av_frame_free(&hw_frame);
		}

		if (hw_frame && av_hwframe_transfer_data(hw_frame, video.av_frame, 0) < 0)
		{
			LOGE("Failed to transfer HW buffer.\n");
			av_frame_free(&hw_frame);
		}

		hw_frame->pts = video.av_frame->pts;
		hw_frame->pict_type = video.av_frame->pict_type;
	}

	ret = avcodec_send_frame(video.av_ctx, hw_frame ? hw_frame : video.av_frame);
	av_frame_free(&hw_frame);

	if (ret < 0)
	{
		LOGE("Failed to send packet to codec: %d\n", ret);
		return false;
	}

	if (!drain_packets(video))
	{
		LOGE("Failed to drain video packets.\n");
		return false;
	}

	audio_compensate_us.store(compensate_audio_us, std::memory_order_relaxed);

#ifdef HAVE_GRANITE_AUDIO
	if (!encode_audio())
	{
		LOGE("Failed to encode audio.\n");
		return false;
	}
#endif

	return true;
}

bool VideoEncoder::Impl::drain_packets(CodecStream &stream)
{
	int ret;
	for (;;)
	{
		ret = avcodec_receive_packet(stream.av_ctx, stream.av_pkt);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
			break;
		else if (ret < 0)
		{
			LOGE("Error encoding frame: %d\n", ret);
			break;
		}

		if (av_format_ctx_local)
		{
			auto *pkt_clone = av_packet_clone(stream.av_pkt);
			pkt_clone->pts = stream.av_pkt->pts;
			pkt_clone->dts = stream.av_pkt->dts;
			pkt_clone->stream_index = stream.av_stream_local->index;
			av_packet_rescale_ts(pkt_clone, stream.av_ctx->time_base, stream.av_stream_local->time_base);
			{
				std::lock_guard<std::mutex> holder{mux_lock};
				ret = av_interleaved_write_frame(av_format_ctx_local, pkt_clone);
			}
			av_packet_free(&pkt_clone);
			if (ret < 0)
			{
				LOGE("Failed to write packet: %d\n", ret);
				break;
			}
		}

		if (stream.av_stream)
		{
			stream.av_pkt->stream_index = stream.av_stream->index;
			av_packet_rescale_ts(stream.av_pkt, stream.av_ctx->time_base, stream.av_stream->time_base);
		}
		else
		{
			av_packet_rescale_ts(stream.av_pkt, stream.av_ctx->time_base, AV_TIME_BASE_Q);
		}

		if (av_format_ctx)
		{
			std::lock_guard<std::mutex> holder{mux_lock};
			ret = av_interleaved_write_frame(av_format_ctx, stream.av_pkt);
			if (ret < 0)
			{
				LOGE("Failed to write packet: %d\n", ret);
				break;
			}
		}
		else if (mux_stream_callback)
		{
			if (&stream == &video)
			{
				mux_stream_callback->write_video_packet(
						stream.av_pkt->pts,
						stream.av_pkt->dts,
						stream.av_pkt->data, stream.av_pkt->size,
						(stream.av_pkt->flags & AV_PKT_FLAG_KEY) != 0);
			}
			else if (&stream == &audio)
			{
				mux_stream_callback->write_audio_packet(
						stream.av_pkt->pts,
						stream.av_pkt->dts,
						stream.av_pkt->data, stream.av_pkt->size);
			}
		}
	}

	return ret == AVERROR_EOF || ret == AVERROR(EAGAIN);
}

void VideoEncoder::set_audio_source(Audio::DumpBackend *backend)
{
	impl->audio_source = backend;
}

void VideoEncoder::set_audio_record_stream(Audio::RecordStream *stream)
{
	impl->audio_stream = stream;
}

bool VideoEncoder::Impl::init_audio_codec()
{
#ifdef HAVE_GRANITE_AUDIO
	AVSampleFormat sample_fmt;
	AVCodecID codec_id;

	// Streaming wants AAC.
	// Just hardcode sample format for what FFmpeg supports.
	// We control which encoders we care about.
	if (options.realtime)
	{
		if (mux_stream_callback)
		{
			// Don't care about streaming platform limitations, so just use the ideal format, ... uncompressed!
			// It's the only true low latency solution. 20ms packets is too much :<
			// Uncompressed stereo audio is about 1.5 mbit/s, no big deal for our purposes.

			if (av_format_ctx_local)
			{
				codec_id = AV_CODEC_ID_OPUS;
				sample_fmt = AV_SAMPLE_FMT_FLT;
				pyro_codec.audio_codec = PYRO_AUDIO_CODEC_OPUS;
			}
			else
			{
				codec_id = AV_CODEC_ID_PCM_S16LE;
				sample_fmt = AV_SAMPLE_FMT_S16;
				pyro_codec.audio_codec = PYRO_AUDIO_CODEC_RAW_S16LE;
			}

			pyro_codec.channels = 2;
			if (options.realtime)
				pyro_codec.rate = uint32_t(audio_stream->get_sample_rate());
			else
				pyro_codec.rate = uint32_t(audio_source->get_sample_rate());
		}
		else
		{
			codec_id = AV_CODEC_ID_AAC;
			sample_fmt = AV_SAMPLE_FMT_FLTP;
		}
	}
	else
	{
		codec_id = AV_CODEC_ID_FLAC;
		sample_fmt = AV_SAMPLE_FMT_S16;
	}

	const AVCodec *codec = avcodec_find_encoder(codec_id);
	if (!codec)
	{
		LOGE("Could not find audio encoder.\n");
		return false;
	}

	if (av_format_ctx)
	{
		audio.av_stream = avformat_new_stream(av_format_ctx, codec);
		if (!audio.av_stream)
		{
			LOGE("Failed to add new stream.\n");
			return false;
		}
	}

	if (av_format_ctx_local)
	{
		audio.av_stream_local = avformat_new_stream(av_format_ctx_local, codec);
		if (!audio.av_stream_local)
		{
			LOGE("Failed to add new stream.\n");
			return false;
		}
	}

	audio.av_ctx = avcodec_alloc_context3(codec);
	if (!audio.av_ctx)
	{
		LOGE("Failed to allocate codec context.\n");
		return false;
	}

	audio.av_ctx->sample_fmt = sample_fmt;

	if (options.realtime)
		audio.av_ctx->sample_rate = int(audio_stream->get_sample_rate());
	else
		audio.av_ctx->sample_rate = int(audio_source->get_sample_rate());

	audio.av_ctx->ch_layout = AV_CHANNEL_LAYOUT_STEREO;

	if (options.realtime)
		audio.av_ctx->time_base = {1, 1000000};
	else
		audio.av_ctx->time_base = {1, audio.av_ctx->sample_rate};

	if (av_format_ctx && (av_format_ctx->oformat->flags & AVFMT_GLOBALHEADER))
		audio.av_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	if (av_format_ctx_local && (av_format_ctx_local->oformat->flags & AVFMT_GLOBALHEADER))
		audio.av_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	if (audio.av_stream)
	{
		audio.av_stream->id = 1;
		audio.av_stream->time_base = audio.av_ctx->time_base;
	}

	if (audio.av_stream_local)
	{
		audio.av_stream_local->id = 1;
		audio.av_stream_local->time_base = audio.av_ctx->time_base;
	}

	if (options.realtime)
		audio.av_ctx->bit_rate = 256 * 1024;

	int ret = avcodec_open2(audio.av_ctx, codec, nullptr);
	if (ret < 0)
	{
		LOGE("Could not open codec: %d\n", ret);
		return false;
	}

	if (audio.av_stream)
		avcodec_parameters_from_context(audio.av_stream->codecpar, audio.av_ctx);
	if (audio.av_stream_local)
		avcodec_parameters_from_context(audio.av_stream_local->codecpar, audio.av_ctx);

	unsigned samples_per_tick;
	if (!options.realtime && (codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE) != 0)
		samples_per_tick = audio_source->get_frames_per_tick();
	else
	{
		samples_per_tick = audio.av_ctx->frame_size;
		if (samples_per_tick == 0)
			samples_per_tick = 256; // About 5ms packets is fine and fits into one pyro UDP packet.
	}

	audio.av_frame = alloc_audio_frame(audio.av_ctx->sample_fmt, audio.av_ctx->ch_layout,
	                                   audio.av_ctx->sample_rate, samples_per_tick);
	if (!audio.av_frame)
	{
		LOGE("Failed to allocate AVFrame.\n");
		return false;
	}

	audio.av_pkt = av_packet_alloc();
	if (!audio.av_pkt)
		return false;

	return true;
#else
	return false;
#endif
}

bool VideoEncoder::Impl::init_video_codec_av(const AVCodec *codec)
{
	if (avcodec_get_hw_config(codec, 0) != nullptr)
	{
		if (!hw.init_codec_context(codec, device, nullptr, nullptr, true))
		{
			LOGW("Failed to init HW encoder context, falling back to software.\n");
			return false;
		}
	}

	if (av_format_ctx)
	{
		video.av_stream = avformat_new_stream(av_format_ctx, codec);
		if (!video.av_stream)
		{
			LOGE("Failed to add new stream.\n");
			return false;
		}
	}

	if (av_format_ctx_local)
	{
		video.av_stream_local = avformat_new_stream(av_format_ctx_local, codec);
		if (!video.av_stream_local)
		{
			LOGE("Failed to add new stream.\n");
			return false;
		}
	}

	video.av_ctx = avcodec_alloc_context3(codec);
	if (!video.av_ctx)
	{
		LOGE("Failed to allocate codec context.\n");
		return false;
	}

	video.av_ctx->width = options.width;
	video.av_ctx->height = options.height;

	switch (options.format)
	{
	case Format::NV12:
		video.av_ctx->pix_fmt = AV_PIX_FMT_NV12;
		break;

	case Format::P016:
		video.av_ctx->pix_fmt = AV_PIX_FMT_P016;
		break;

	case Format::P010:
		video.av_ctx->pix_fmt = AV_PIX_FMT_P010;
		break;

	default:
		return false;
	}

	if (hw.get_pix_fmt() != AV_PIX_FMT_NONE)
	{
		if (hw.init_frame_context(video.av_ctx, options.width, options.height, video.av_ctx->pix_fmt))
			video.av_ctx->pix_fmt = AVPixelFormat(hw.get_pix_fmt());
		else
			hw.reset();
	}

	video.av_ctx->framerate = { options.frame_timebase.den, options.frame_timebase.num };

	if (options.low_latency)
		video.av_ctx->max_b_frames = 0;

	if (mux_stream_callback && options.low_latency)
	{
		// Don't attempt to smooth out PTS values.
		video.av_ctx->time_base = AV_TIME_BASE_Q;
	}
	else if (options.realtime)
	{
		video.ticks_per_frame = 16;
		video.av_ctx->time_base = {options.frame_timebase.num, options.frame_timebase.den * video.ticks_per_frame};

#if defined(_MSC_VER)
#define DISABLE_DEPRECATION_WARNINGS __pragma(warning(push)) __pragma(warning(disable:4996))
#define ENABLE_DEPRECATION_WARNINGS  __pragma(warning(pop))
#else
#define DISABLE_DEPRECATION_WARNINGS _Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"")
#define ENABLE_DEPRECATION_WARNINGS  _Pragma("GCC diagnostic pop")
#endif

		// NVENC B-frames break without this since it computes DTS by subtracting PTS by ticks_per_frame * N factor.
		DISABLE_DEPRECATION_WARNINGS
#if LIBAVCODEC_VERSION_MAJOR < 61
		video.av_ctx->ticks_per_frame = 16;
#else
#warning "Not using av_ctx->ticks_per_frame, NVENC b-frames will break!"
#endif
		ENABLE_DEPRECATION_WARNINGS
	}
	else
	{
		video.av_ctx->time_base = { options.frame_timebase.num, options.frame_timebase.den };
		video.ticks_per_frame = 1;
	}

	video.av_ctx->color_range = AVCOL_RANGE_MPEG;

	if (options.hdr10)
	{
		video.av_ctx->colorspace = AVCOL_SPC_BT2020_NCL;
		video.av_ctx->color_primaries = AVCOL_PRI_BT2020;
		video.av_ctx->color_trc = AVCOL_TRC_SMPTE2084; // PQ
	}
	else
	{
		video.av_ctx->colorspace = AVCOL_SPC_BT709;
		video.av_ctx->color_primaries = AVCOL_PRI_BT709;
	}

	switch (options.siting)
	{
	case ChromaSiting::TopLeft:
		video.av_ctx->chroma_sample_location = AVCHROMA_LOC_TOPLEFT;
		break;

	case ChromaSiting::Left:
		video.av_ctx->chroma_sample_location = AVCHROMA_LOC_LEFT;
		break;

	case ChromaSiting::Center:
		video.av_ctx->chroma_sample_location = AVCHROMA_LOC_CENTER;
		break;

	default:
		return false;
	}

	if (av_format_ctx && (av_format_ctx->oformat->flags & AVFMT_GLOBALHEADER))
		video.av_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	// TODO: Is mismatch a problem?
	if (av_format_ctx_local && (av_format_ctx_local->oformat->flags & AVFMT_GLOBALHEADER))
		video.av_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	if (video.av_stream)
	{
		video.av_stream->id = 0;
		video.av_stream->time_base = video.av_ctx->time_base;
	}

	if (video.av_stream_local)
	{
		video.av_stream_local->id = 0;
		video.av_stream_local->time_base = video.av_ctx->time_base;
	}

	AVDictionary *opts = nullptr;

	bool is_x264 = strcmp(options.encoder, "libx264") == 0;
	bool is_nvenc = strstr(options.encoder, "nvenc") != nullptr;
	bool is_av1_vaapi = strcmp(options.encoder, "av1_vaapi") == 0;

	if (is_av1_vaapi)
	{
		av_dict_set(&opts, "profile", "main", 0);
		av_dict_set(&opts, "level", "6.3", 0);
		av_dict_set(&opts, "tier", "high", 0);
		if (options.low_latency)
			av_dict_set_int(&opts, "async_depth", 1, 0);
	}

	if (options.realtime || !is_x264)
	{
		video.av_ctx->bit_rate = options.realtime_options.bitrate_kbits * 1000;
		video.av_ctx->rc_buffer_size = options.realtime_options.vbv_size_kbits * 1000;
		video.av_ctx->rc_max_rate = options.realtime_options.max_bitrate_kbits * 1000;
		video.av_ctx->gop_size = int(options.realtime_options.gop_seconds *
				float(video.av_ctx->framerate.num) / float(video.av_ctx->framerate.den));

		// TODO: Is there a way to express infinite GOP here?
		if (video.av_ctx->gop_size < 0)
			video.av_ctx->gop_size = 120;
		else if (video.av_ctx->gop_size == 0)
			video.av_ctx->gop_size = 1;

		if (is_x264)
		{
			if (options.realtime_options.x264_preset)
				av_dict_set(&opts, "preset", options.realtime_options.x264_preset, 0);
			if (options.realtime_options.x264_tune)
				av_dict_set(&opts, "tune", options.realtime_options.x264_tune, 0);
			if (options.realtime_options.threads)
				av_dict_set_int(&opts, "threads", options.realtime_options.threads, 0);
		}
		else if (is_nvenc)
		{
			// Codec delay. We want blocking realtime.
			av_dict_set_int(&opts, "delay", 0, 0);
			if (options.low_latency)
			{
				av_dict_set_int(&opts, "zerolatency", 1, 0);
				av_dict_set(&opts, "rc", "cbr", 0);
				av_dict_set(&opts, "preset", "p1", 0);
				av_dict_set(&opts, "tune", "ll", 0);
			}
			else
			{
				av_dict_set(&opts, "rc", "vbr", 0);
				av_dict_set(&opts, "tune", "hq", 0);
				av_dict_set(&opts, "preset", "p7", 0);
			}
		}

		if ((is_x264 || is_nvenc) && options.low_latency)
		{
			av_dict_set_int(&opts, "intra-refresh", 1, 0);
			av_dict_set_int(&opts, "forced-idr", 1, 0);
			video.av_ctx->refs = 1;
		}
	}
	else
	{
		av_dict_set(&opts, "preset", "fast", 0);
		av_dict_set_int(&opts, "crf", 18, 0);
	}

	int ret = avcodec_open2(video.av_ctx, codec, &opts);
	av_dict_free(&opts);

	if (ret < 0)
	{
		LOGE("Could not open codec: %d\n", ret);
		return false;
	}

	if (video.av_stream)
		avcodec_parameters_from_context(video.av_stream->codecpar, video.av_ctx);
	if (video.av_stream_local)
		avcodec_parameters_from_context(video.av_stream_local->codecpar, video.av_ctx);

	if (hw.get_hw_device_type() == AV_HWDEVICE_TYPE_NONE)
	{
		video.av_frame = alloc_video_frame(video.av_ctx->pix_fmt, options.width, options.height);
		if (!video.av_frame)
		{
			LOGE("Failed to allocate AVFrame.\n");
			return false;
		}
	}
#ifdef HAVE_FFMPEG_VULKAN
	else if (hw.get_hw_device_type() == AV_HWDEVICE_TYPE_VULKAN)
	{
		// Do not allocate av_frame, we'll convert YUV into the frame context.
	}
#endif
	else
	{
		video.av_frame = alloc_video_frame(AVPixelFormat(hw.get_sw_pix_fmt()), options.width, options.height);
		if (!video.av_frame)
		{
			LOGE("Failed to allocate AVFrame.\n");
			return false;
		}
	}

	const auto translate_codec_id = [](AVCodecID id) {
		switch (id)
		{
		case AV_CODEC_ID_H264:
			return PYRO_VIDEO_CODEC_H264;
		case AV_CODEC_ID_H265:
			return PYRO_VIDEO_CODEC_H265;
		case AV_CODEC_ID_AV1:
			return PYRO_VIDEO_CODEC_AV1;
		default:
			LOGW("Unknown video codec %d.\n", id);
			return PYRO_VIDEO_CODEC_NONE;
		}
	};

	pyro_codec.video_codec = translate_codec_id(video.av_ctx->codec_id);
	pyro_codec.width = video.av_ctx->width;
	pyro_codec.height = video.av_ctx->height;
	pyro_codec.frame_rate_num = video.av_ctx->framerate.num;
	pyro_codec.frame_rate_den = video.av_ctx->framerate.den;

	video.av_pkt = av_packet_alloc();
	if (!video.av_pkt)
		return false;
	return true;
}

bool VideoEncoder::Impl::init_video_codec_pyro(PyroEnc::Profile profile)
{
	PyroEnc::EncoderCreateInfo pyro_info = {};
	pyro_info.profile = profile;
	pyro_info.hints.tuning = options.low_latency ?
			VK_VIDEO_ENCODE_TUNING_MODE_LOW_LATENCY_KHR : VK_VIDEO_ENCODE_TUNING_MODE_HIGH_QUALITY_KHR;
	pyro_info.hints.usage = options.low_latency ?
			VK_VIDEO_ENCODE_USAGE_STREAMING_BIT_KHR : VK_VIDEO_ENCODE_USAGE_RECORDING_BIT_KHR;
	pyro_info.hints.content = VK_VIDEO_ENCODE_CONTENT_RENDERED_BIT_KHR;

	pyro_info.frame_rate_num = options.frame_timebase.den;
	pyro_info.frame_rate_den = options.frame_timebase.num;
	pyro_info.width = options.width;
	pyro_info.height = options.height;
	pyro_info.device = device->get_device();
	pyro_info.instance = device->get_instance();
	pyro_info.gpu = device->get_physical_device();
	pyro_info.get_instance_proc_addr = Vulkan::Context::get_instance_proc_addr();
	pyro_info.encode_queue.queue = device->get_queue_info().queues[Vulkan::QUEUE_INDEX_VIDEO_ENCODE];
	pyro_info.encode_queue.family_index = device->get_queue_info().family_indices[Vulkan::QUEUE_INDEX_VIDEO_ENCODE];
	pyro_info.conversion_queue.queue = device->get_queue_info().queues[Vulkan::QUEUE_INDEX_COMPUTE];
	pyro_info.conversion_queue.family_index = device->get_queue_info().family_indices[Vulkan::QUEUE_INDEX_COMPUTE];

	if (pyro_encoder.init_encoder(pyro_info) != PyroEnc::Result::Success)
	{
		LOGE("Failed to initialize pyro encoder.\n");
		return false;
	}

	PyroEnc::RateControlInfo rate_info = {};
	rate_info.mode = options.low_latency ? PyroEnc::RateControlMode::CBR : PyroEnc::RateControlMode::VBR;
	rate_info.bitrate_kbits = options.realtime_options.bitrate_kbits;
	rate_info.max_bitrate_kbits = options.realtime_options.max_bitrate_kbits;

	if (options.realtime_options.gop_seconds < 0.0f)
	{
		rate_info.gop_frames = UINT32_MAX;
	}
	else
	{
		rate_info.gop_frames = unsigned(
				options.realtime_options.gop_seconds * float(pyro_info.frame_rate_num) /
				float(pyro_info.frame_rate_den));
	}

	if (!pyro_encoder.set_rate_control_info(rate_info))
	{
		LOGE("Failed to set rate control info.\n");
		return false;
	}

	switch (profile)
	{
	case PyroEnc::Profile::H264_High:
		pyro_codec.video_codec = PYRO_VIDEO_CODEC_H264;
		break;

	case PyroEnc::Profile::H265_Main:
	case PyroEnc::Profile::H265_Main10:
		pyro_codec.video_codec = PYRO_VIDEO_CODEC_H265;
		break;

	default:
		return false;
	}

	pyro_codec.width = pyro_info.width;
	pyro_codec.height = pyro_info.height;
	pyro_codec.frame_rate_num = pyro_info.frame_rate_num;
	pyro_codec.frame_rate_den = pyro_info.frame_rate_den;
	pyro_codec.video_color_profile = PYRO_VIDEO_COLOR_BT709_LIMITED_LEFT_CHROMA_420;

	LOGI("Initialized PyroEnc encoder.\n");

	return true;
}

bool VideoEncoder::Impl::init_video_codec()
{
	const AVCodec *codec = avcodec_find_encoder_by_name(options.encoder);

	PyroEnc::Profile pyro_profile = {};

	// Only allow PyroEnc path for pure streaming scenario for now.
	if (!codec && options.realtime &&
	    mux_stream_callback && !options.realtime_options.local_backup_path &&
	    options.encoder && (strcmp(options.encoder, "h264_pyro") == 0 || strcmp(options.encoder, "h265_pyro") == 0))
	{
		// Use custom.
		using_pyro_encoder = true;

		auto &ext = device->get_device_features();

		if (options.format == Format::NV12 &&
		    ext.supports_video_encode_h264 &&
		    strcmp(options.encoder, "h264_pyro") == 0)
		{
			pyro_profile = PyroEnc::Profile::H264_High;
		}
		else if (options.format == Format::NV12 &&
		         ext.supports_video_encode_h265 &&
		         strcmp(options.encoder, "h265_pyro") == 0)
		{
			pyro_profile = PyroEnc::Profile::H265_Main;
		}
		else if ((options.format == Format::P010 || options.format == Format::P016) &&
		         ext.supports_video_encode_h265 &&
		         strcmp(options.encoder, "h265_pyro") == 0)
		{
			pyro_profile = PyroEnc::Profile::H265_Main10;
		}
		else
		{
			LOGE("Could not find supported pyroenc profile for requested codec.\n");
			return false;
		}
	}

	if (!codec && !using_pyro_encoder)
	{
		LOGE("Could not find requested encoder \"%s\".\n", options.encoder);
		return false;
	}

	if (codec)
		return init_video_codec_av(codec);
	else
		return init_video_codec_pyro(pyro_profile);
}

bool VideoEncoder::Impl::init(Vulkan::Device *device_, const char *path, const Options &options_)
{
	device = device_;
	options = options_;
	audio_compensate_us = 0;

	// For file-less formats like RTMP need to specify muxer format.
	const char *muxer = nullptr;
	if (options.realtime && options.realtime_options.muxer_format)
		muxer = options.realtime_options.muxer_format;

	if ((!path && !mux_stream_callback) || (path && mux_stream_callback))
	{
		LOGE("Must either use a proper encode path, or mux stream callback.\n");
		return false;
	}

	const auto cleanup_format_context = [this]()
	{
		if (av_format_ctx)
		{
			if (!(av_format_ctx->flags & AVFMT_NOFILE))
				avio_closep(&av_format_ctx->pb);
			avformat_free_context(av_format_ctx);
			av_format_ctx = nullptr;
		}

		if (av_format_ctx_local)
		{
			if (!(av_format_ctx_local->flags & AVFMT_NOFILE))
				avio_closep(&av_format_ctx_local->pb);
			avformat_free_context(av_format_ctx_local);
			av_format_ctx_local = nullptr;
		}
	};

	int ret;
	if (path)
	{
		if ((ret = avformat_alloc_output_context2(&av_format_ctx, nullptr, muxer, path)) < 0)
		{
			LOGE("Failed to open format context: %d\n", ret);
			return false;
		}
	}

	if (options.realtime && options.realtime_options.local_backup_path)
	{
		if ((ret = avformat_alloc_output_context2(&av_format_ctx_local, nullptr, nullptr,
		                                          options.realtime_options.local_backup_path)) < 0)
		{
			LOGE("Failed to open format context: %d\n", ret);
			return false;
		}
	}

	if (!init_video_codec())
	{
		cleanup_format_context();
		return false;
	}

	if ((options.realtime && audio_stream) || (!options.realtime && audio_source))
	{
		if (!init_audio_codec())
		{
			cleanup_format_context();
			return false;
		}
	}

	if (mux_stream_callback)
		mux_stream_callback->set_codec_parameters(pyro_codec);

#ifdef HAVE_GRANITE_AUDIO
	if (audio_stream)
		audio_stream->set_record_callback(this);
#endif

	if (av_format_ctx)
		av_dump_format(av_format_ctx, 0, path, 1);
	if (av_format_ctx_local)
		av_dump_format(av_format_ctx_local, 0, options.realtime_options.local_backup_path, 1);

	AVDictionary *muxer_opts = nullptr;

	// Special flag for mpegts that lowers latency according to the intertubes.
	if (muxer && strcmp(muxer, "mpegts") == 0)
		av_dict_set_int(&muxer_opts, "omit_video_pes_length", 0, 0);

	const auto open_file = [this, &muxer_opts](AVFormatContext *ctx, const char *encode_path) -> bool
	{
		int retval;

		if (!(ctx->flags & AVFMT_NOFILE))
		{
			retval = avio_open(&ctx->pb, encode_path, AVIO_FLAG_WRITE);
			if (retval < 0)
			{
				LOGE("Could not open file: %d\n", retval);
				return false;
			}
		}

		if ((retval = avformat_write_header(ctx, ctx == av_format_ctx ? &muxer_opts : nullptr)) < 0)
		{
			LOGE("Failed to write format header: %d\n", retval);
			return false;
		}

		return true;
	};

	if (av_format_ctx && !open_file(av_format_ctx, path))
	{
		cleanup_format_context();
		return false;
	}

	if (av_format_ctx_local && !open_file(av_format_ctx_local, options.realtime_options.local_backup_path))
	{
		cleanup_format_context();
		return false;
	}

	av_dict_free(&muxer_opts);

	realtime_pts.base_pts = int64_t(Util::get_current_time_nsecs() / 1000);

	return true;
}

AVFrame *VideoEncoder::Impl::alloc_audio_frame(
		AVSampleFormat samp_format, AVChannelLayout channel_layout,
		unsigned sample_rate, unsigned sample_count)
{
	AVFrame *frame = av_frame_alloc();
	if (!frame)
		return nullptr;

	frame->ch_layout = channel_layout;
	frame->format = samp_format;
	frame->sample_rate = sample_rate;
	frame->nb_samples = sample_count;

	int ret;
	if ((ret = av_frame_get_buffer(frame, 0)) < 0)
	{
		LOGE("Failed to allocate frame buffer: %d.\n", ret);
		av_frame_free(&frame);
		return nullptr;
	}

	return frame;
}

AVFrame *VideoEncoder::Impl::alloc_video_frame(AVPixelFormat pix_fmt, unsigned width, unsigned height)
{
	AVFrame *frame = av_frame_alloc();
	if (!frame)
		return nullptr;

	frame->width = width;
	frame->height = height;
	frame->format = pix_fmt;

	int ret;
	if ((ret = av_frame_get_buffer(frame, 0)) < 0)
	{
		LOGE("Failed to allocate frame buffer: %d.\n", ret);
		av_frame_free(&frame);
		return nullptr;
	}

	return frame;
}

#ifdef HAVE_FFMPEG_VULKAN
struct FrameLock
{
	AVHWFramesContext *frames;
	AVVulkanFramesContext *vk;
	AVVkFrame *vk_frame;
	inline void lock() const { if (vk && vk_frame) vk->lock_frame(frames, vk_frame); }
	inline void unlock() const { if (vk && vk_frame) vk->unlock_frame(frames, vk_frame); }
};

void VideoEncoder::Impl::submit_process_rgb_vulkan(Vulkan::CommandBufferHandle &cmd,
                                                   Granite::VideoEncoder::YCbCrPipelineData &pipeline)
{
	auto *frames = reinterpret_cast<AVHWFramesContext *>(video.av_ctx->hw_frames_ctx->data);
	auto *vk = static_cast<AVVulkanFramesContext *>(frames->hwctx);
	auto *vk_frame = reinterpret_cast<AVVkFrame *>(pipeline.hw_frame->data[0]);

	// Docs suggest we have to lock the AVVkFrame when accessing the frame struct.
	FrameLock l = { frames, vk, vk_frame };
	std::lock_guard<FrameLock> holder{l};

	auto sem = device->request_semaphore(VK_SEMAPHORE_TYPE_TIMELINE, vk_frame->sem[0], false);
	auto acq_binary = device->request_timeline_semaphore_as_binary(*sem, vk_frame->sem_value[0]);
	acq_binary->signal_external();
	auto rel_binary = device->request_timeline_semaphore_as_binary(*sem, ++vk_frame->sem_value[0]);

	device->add_wait_semaphore(cmd->get_command_buffer_type(), std::move(acq_binary),
							   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, true);

	auto type = cmd->get_command_buffer_type();
	device->submit(cmd, &pipeline.fence);
	device->submit_empty(type, nullptr, rel_binary.get());
}
#endif

void VideoEncoder::Impl::submit_process_rgb_pyro(Vulkan::CommandBufferHandle &cmd,
                                                 VideoEncoder::YCbCrPipelineData &pipeline)
{
	if (cmd->get_command_buffer_type() == Vulkan::CommandBuffer::Type::AsyncCompute)
	{
		device->submit(cmd, &pipeline.fence);
	}
	else
	{
		Vulkan::Semaphore sem;
		device->submit(cmd, &pipeline.fence, 1, &sem);
		device->add_wait_semaphore(Vulkan::CommandBuffer::Type::AsyncCompute, std::move(sem),
		                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, true);
	}
}

void VideoEncoder::Impl::submit_process_rgb_readback(Vulkan::CommandBufferHandle &cmd,
                                                     VideoEncoder::YCbCrPipelineData &pipeline)
{
	if (pipeline.fence)
		pipeline.fence->wait();
	pipeline.fence.reset();
	device->submit(cmd, &pipeline.fence);
}

VideoEncoder::VideoEncoder()
{
	impl.reset(new Impl);
}

VideoEncoder::~VideoEncoder()
{
}

bool VideoEncoder::init(Vulkan::Device *device, const char *path, const Options &options)
{
	return impl->init(device, path, options);
}

void VideoEncoder::set_mux_stream_callback(Granite::MuxStreamCallback *callback)
{
	impl->set_mux_stream_callback(callback);
}

void VideoEncoder::process_rgb_pyro(Vulkan::CommandBuffer &cmd, VideoEncoder::YCbCrPipeline &pipeline_ptr,
                                    const Vulkan::ImageView &view)
{
	auto &pipeline = *pipeline_ptr;
	cmd.image_barrier(*pipeline.luma, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
	                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
	                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

	cmd.set_program(pipeline.rgb_scale);
	cmd.set_specialization_constant_mask(1);

	if (Vulkan::format_is_srgb(view.get_format()) || view.get_format() == VK_FORMAT_R16G16B16A16_SFLOAT)
	{
		cmd.set_specialization_constant(0, 1);
		cmd.set_texture(0, 0, view, Vulkan::StockSampler::NearestClamp);
	}
	else
	{
		cmd.set_specialization_constant(0, 0);
		cmd.set_texture(0, 0, view, Vulkan::StockSampler::NearestClamp);
	}

	cmd.set_storage_texture(0, 1, pipeline.luma->get_view());

	struct Push
	{
		uint32_t width, height;
		float inv_width, inv_height;
		float input_width, input_height;
		float inv_input_width, inv_input_height;
		float dither_strength;
	} push = {};

	push.width = impl->options.width;
	push.height = impl->options.height;
	push.inv_width = pipeline.constants.inv_resolution_luma[0];
	push.inv_height = pipeline.constants.inv_resolution_luma[1];
	push.input_width = float(view.get_view_width());
	push.input_height = float(view.get_view_height());
	push.inv_input_width = 1.0f / push.input_width;
	push.inv_input_height = 1.0f / push.input_height;
	push.dither_strength = pipeline.constants.dither_strength;
	cmd.push_constants(&push, 0, sizeof(push));

	auto start_yuv_ts = cmd.write_timestamp(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
	cmd.dispatch(pipeline.constants.luma_dispatch[0], pipeline.constants.luma_dispatch[1], 1);
	auto end_yuv_ts = cmd.write_timestamp(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
	cmd.image_barrier(*pipeline.luma, VK_IMAGE_LAYOUT_GENERAL, impl->pyro_encoder.get_conversion_image_layout(),
	                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
	                  impl->pyro_encoder.get_conversion_dst_stage(), impl->pyro_encoder.get_conversion_dst_access());
	cmd.get_device().register_time_interval("GPU", std::move(start_yuv_ts), std::move(end_yuv_ts), "rgb-scale");
	cmd.set_specialization_constant_mask(0);
}

void VideoEncoder::process_rgb(Vulkan::CommandBuffer &cmd, YCbCrPipeline &pipeline_ptr, const Vulkan::ImageView &view)
{
	auto &pipeline = *pipeline_ptr;

	if (pipeline.fence)
		pipeline.fence->wait();
	pipeline.fence.reset();

	if (pipeline.pyroenc)
	{
		process_rgb_pyro(cmd, pipeline_ptr, view);
		return;
	}

	if (pipeline.hw_frame)
		av_frame_free(&pipeline.hw_frame);

	Vulkan::ImageViewHandle wrapped_planes[2];
	Vulkan::ImageHandle wrapped_image;

#ifdef HAVE_FFMPEG_VULKAN
	auto &device = cmd.get_device();
	AVHWFramesContext *frames = nullptr;
	AVVulkanFramesContext *vk = nullptr;
	AVVkFrame *vk_frame = nullptr;

	if (impl->hw.get_pix_fmt() == AV_PIX_FMT_VULKAN)
	{
		frames = reinterpret_cast<AVHWFramesContext *>(impl->video.av_ctx->hw_frames_ctx->data);
		vk = static_cast<AVVulkanFramesContext *>(frames->hwctx);

		pipeline.hw_frame = av_frame_alloc();
		if (av_hwframe_get_buffer(impl->video.av_ctx->hw_frames_ctx, pipeline.hw_frame, 0) < 0)
			LOGE("Failed to get HW buffer.\n");
		else
			vk_frame = reinterpret_cast<AVVkFrame *>(pipeline.hw_frame->data[0]);
	}

	// Docs suggest we have to lock the AVVkFrame when accessing the frame struct.
	FrameLock l = { frames, vk, vk_frame };
	std::lock_guard<FrameLock> holder{l};

	if (vk_frame)
	{
		Vulkan::ImageCreateInfo info = {};
		info.type = VK_IMAGE_TYPE_2D;
		// Extent parameters aren't necessarily quite correct,
		// but we don't really care since we're just creating temporary views.
		info.width = impl->video.av_ctx->width;
		info.height = impl->video.av_ctx->height;
		info.depth = 1;
		info.format = vk->format[0];
		info.usage = vk->usage;
		info.flags = vk->img_flags;
		info.layers = 1;
		info.levels = 1;
		info.domain = Vulkan::ImageDomain::Physical;
		info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		wrapped_image = device.wrap_image(info, vk_frame->img[0]);

		Vulkan::ImageViewCreateInfo view_info = {};
		view_info.image = wrapped_image.get();
		view_info.view_type = VK_IMAGE_VIEW_TYPE_2D;

		view_info.aspect = VK_IMAGE_ASPECT_PLANE_0_BIT;
		view_info.format = VK_FORMAT_R8_UNORM;
		wrapped_planes[0] = device.create_image_view(view_info);

		view_info.aspect = VK_IMAGE_ASPECT_PLANE_1_BIT;
		view_info.format = VK_FORMAT_R8G8_UNORM;
		wrapped_planes[1] = device.create_image_view(view_info);

		vk_frame->layout[0] = VK_IMAGE_LAYOUT_GENERAL;
		// XXX: FFmpeg header bug.
		// Semaphore ensures memory avail / vis.
		vk_frame->access[0] = VkAccessFlagBits(0);
	}
#endif

	if (wrapped_image)
	{
		cmd.image_barrier(*wrapped_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
		                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
		                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
	}

	if (pipeline.luma)
	{
		cmd.image_barrier(*pipeline.luma, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
		                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
		                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
	}

	cmd.image_barrier(*pipeline.chroma_full, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
	                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
	                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

	if (pipeline.chroma)
	{
		cmd.image_barrier(*pipeline.chroma, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
		                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
		                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
	}

	cmd.set_program(pipeline.rgb_to_ycbcr);

	if (Vulkan::format_is_srgb(view.get_format()))
	{
		cmd.set_unorm_texture(0, 0, view);
		cmd.set_sampler(0, 0, Vulkan::StockSampler::NearestClamp);
	}
	else
		cmd.set_texture(0, 0, view, Vulkan::StockSampler::NearestClamp);

	cmd.set_storage_texture(0, 1, wrapped_planes[0] ? *wrapped_planes[0] : pipeline.luma->get_view());
	cmd.set_storage_texture(0, 2, pipeline.chroma_full->get_view());

	struct Push
	{
		uint32_t width, height;
		float base_u, base_v;
		float inv_width, inv_height;
		float input_width, input_height;
		float inv_input_width, inv_input_height;
		float dither_strength;
	} push = {};

	push.width = impl->video.av_ctx->width;
	push.height = impl->video.av_ctx->height;
	push.inv_width = pipeline.constants.inv_resolution_luma[0];
	push.inv_height = pipeline.constants.inv_resolution_luma[1];
	push.input_width = float(view.get_view_width());
	push.input_height = float(view.get_view_height());
	push.inv_input_width = 1.0f / push.input_width;
	push.inv_input_height = 1.0f / push.input_height;
	push.base_u = pipeline.constants.base_uv_luma[0];
	push.base_v = pipeline.constants.base_uv_luma[1];
	push.dither_strength = pipeline.constants.dither_strength;
	cmd.push_constants(&push, 0, sizeof(push));

	auto *color_transform = cmd.allocate_typed_constant_data<vec4>(0, 3, 3);

	// Minimal crude implementation to get something HDR10 working.
	if (impl->options.hdr10)
	{
		color_transform[0] = vec4(0.2627f, 0.678f, 0.0593f, 0.0f);
		color_transform[1] = vec4(-0.13963f, -0.36037f, 0.5f, 0.0f);
		color_transform[2] = vec4(0.5f, -0.459786f, -0.0402143f, 0.0f);
	}
	else
	{
		color_transform[0] = vec4(0.2126f, 0.7152f, 0.0722f, 0.0f);
		color_transform[1] = vec4(-0.114572f, -0.385428f, 0.5f, 0.0f);
		color_transform[2] = vec4(0.5f, -0.454153f, -0.0458471f, 0.0f);
	}

	auto start_yuv_ts = cmd.write_timestamp(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
	cmd.dispatch(pipeline.constants.luma_dispatch[0], pipeline.constants.luma_dispatch[1], 1);
	auto end_yuv_ts = cmd.write_timestamp(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

	if (pipeline.luma)
	{
		cmd.image_barrier(*pipeline.luma, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
		                  VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_TRANSFER_READ_BIT);
	}

	cmd.image_barrier(*pipeline.chroma_full, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
					  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

	cmd.set_program(pipeline.chroma_downsample);
	cmd.set_texture(0, 0, pipeline.chroma_full->get_view(), Vulkan::StockSampler::LinearClamp);
	cmd.set_storage_texture(0, 1, wrapped_planes[1] ? *wrapped_planes[1] : pipeline.chroma->get_view());

	push.inv_width = pipeline.constants.inv_resolution_chroma[0];
	push.inv_height = pipeline.constants.inv_resolution_chroma[1];
	push.base_u = pipeline.constants.base_uv_chroma[0];
	push.base_v = pipeline.constants.base_uv_chroma[1];
	cmd.push_constants(&push, 0, sizeof(push));
	auto start_chroma_ts = cmd.write_timestamp(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
	cmd.dispatch(pipeline.constants.chroma_dispatch[0], pipeline.constants.chroma_dispatch[1], 1);
	auto end_chroma_ts = cmd.write_timestamp(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

	if (pipeline.chroma)
	{
		cmd.image_barrier(*pipeline.chroma, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
		                  VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_TRANSFER_READ_BIT);

		cmd.copy_image_to_buffer(*pipeline.buffer, *pipeline.luma, pipeline.planes[0].offset,
		                         {},
		                         {pipeline.luma->get_width(), pipeline.luma->get_height(), 1},
		                         pipeline.planes[0].row_length, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1});

		cmd.copy_image_to_buffer(*pipeline.buffer, *pipeline.chroma, pipeline.planes[1].offset,
		                         {},
		                         {pipeline.chroma->get_width(), pipeline.chroma->get_height(), 1},
		                         pipeline.planes[1].row_length, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1});

		cmd.barrier(VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
		            VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
	}

	cmd.get_device().register_time_interval("GPU", std::move(start_yuv_ts), std::move(end_yuv_ts), "rgb-to-yuv");
	cmd.get_device().register_time_interval("GPU", std::move(start_chroma_ts), std::move(end_chroma_ts), "chroma-downsample");
}

bool VideoEncoder::encode_frame(YCbCrPipeline &pipeline_ptr, int64_t pts, int compensate_audio_us)
{
	auto &pipeline = *pipeline_ptr;
	if (!pipeline.fence)
		return false;

	bool ret;

	if (pipeline.pyroenc)
	{
		ret = impl->encode_frame(pipeline.luma->get_view(), pts, compensate_audio_us);
	}
	else if (pipeline.hw_frame)
	{
		ret = impl->encode_frame(pipeline.hw_frame, pts, compensate_audio_us);
		av_frame_free(&pipeline.hw_frame);

		// We only wait for the YUV processing to complete here, not encoding itself.
		// These encode tasks should run in threads anyway.
		pipeline.fence->wait();
	}
	else
	{
		pipeline.fence->wait();
		auto *buf = static_cast<const uint8_t *>(impl->device->map_host_buffer(
				*pipeline.buffer, Vulkan::MEMORY_ACCESS_READ_BIT));
		ret = impl->encode_frame(buf, pipeline.planes, pipeline.num_planes, pts, compensate_audio_us);
		impl->device->unmap_host_buffer(*pipeline.buffer, Vulkan::MEMORY_ACCESS_READ_BIT);
	}

	return ret;
}

void VideoEncoder::submit_process_rgb(Vulkan::CommandBufferHandle &cmd, YCbCrPipeline &pipeline_ptr)
{
	auto &pipeline = *pipeline_ptr;

	if (pipeline.pyroenc)
		impl->submit_process_rgb_pyro(cmd, pipeline);
	else
#ifdef HAVE_FFMPEG_VULKAN
	if (pipeline.hw_frame)
	{
		impl->submit_process_rgb_vulkan(cmd, pipeline);
	}
	else
#endif
	{
		impl->submit_process_rgb_readback(cmd, pipeline);
	}
}

VideoEncoder::YCbCrPipeline VideoEncoder::create_ycbcr_pipeline(const FFmpegEncode::Shaders<> &shaders) const
{
	YCbCrPipeline pipeline_ptr{new YCbCrPipelineData};
	auto &pipeline = *pipeline_ptr;

	pipeline.rgb_to_ycbcr = shaders.rgb_to_yuv;
	pipeline.chroma_downsample = shaders.chroma_downsample;
	pipeline.rgb_scale = shaders.rgb_scale;

	VkFormat luma_format, chroma_format;
	unsigned pixel_size;

	if (impl->using_pyro_encoder)
	{
		luma_format = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
		chroma_format = VK_FORMAT_UNDEFINED;
		pixel_size = 4;
		pipeline.constants.dither_strength = 1.0f / 1023.0f;
		pipeline.pyroenc = true;
	}
	else if (impl->options.format != Format::NV12)
	{
		luma_format = VK_FORMAT_R16_UNORM;
		chroma_format = VK_FORMAT_R16G16_UNORM;
		pixel_size = 2;
		pipeline.constants.dither_strength = 1.0f / 1023.0f;
	}
	else
	{
		luma_format = VK_FORMAT_R8_UNORM;
		chroma_format = VK_FORMAT_R8G8_UNORM;
		pixel_size = 1;
		pipeline.constants.dither_strength = 1.0f / 255.0f;
	}

	auto image_info = Vulkan::ImageCreateInfo::immutable_2d_image(impl->options.width, impl->options.height, luma_format);
	image_info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	image_info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

	if (impl->using_pyro_encoder)
	{
		image_info.misc = Vulkan::IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_COMPUTE_BIT |
		                  Vulkan::IMAGE_MISC_CONCURRENT_QUEUE_GRAPHICS_BIT;
	}

#ifdef HAVE_FFMPEG_VULKAN
	if (impl->hw.get_pix_fmt() != AV_PIX_FMT_VULKAN)
#endif
	{
		pipeline.luma = impl->device->create_image(image_info);
		impl->device->set_name(*pipeline.luma, "video-encode-luma");
	}

	VkDeviceSize total_size = 0;
	VkDeviceSize aligned_width = (image_info.width + 63) & ~63;
	VkDeviceSize luma_size = aligned_width * image_info.height * pixel_size;

	pipeline.planes[pipeline.num_planes].offset = total_size;
	pipeline.planes[pipeline.num_planes].stride = aligned_width * pixel_size;
	pipeline.planes[pipeline.num_planes].row_length = aligned_width;
	pipeline.num_planes++;
	total_size += luma_size;

	pipeline.constants.inv_resolution_luma[0] = 1.0f / float(image_info.width);
	pipeline.constants.inv_resolution_luma[1] = 1.0f / float(image_info.height);
	pipeline.constants.base_uv_luma[0] = 0.5f / float(image_info.width);
	pipeline.constants.base_uv_luma[1] = 0.5f / float(image_info.height);
	pipeline.constants.luma_dispatch[0] = (image_info.width + 7) / 8;
	pipeline.constants.luma_dispatch[1] = (image_info.height + 7) / 8;

	pipeline.constants.inv_resolution_chroma[0] = 2.0f * pipeline.constants.inv_resolution_luma[0];
	pipeline.constants.inv_resolution_chroma[1] = 2.0f * pipeline.constants.inv_resolution_luma[1];

	switch (impl->options.siting)
	{
	case ChromaSiting::Center:
		pipeline.constants.base_uv_chroma[0] = 1.0f / float(image_info.width);
		pipeline.constants.base_uv_chroma[1] = 1.0f / float(image_info.height);
		break;

	case ChromaSiting::TopLeft:
		pipeline.constants.base_uv_chroma[0] = 0.5f / float(image_info.width);
		pipeline.constants.base_uv_chroma[1] = 0.5f / float(image_info.height);
		break;

	case ChromaSiting::Left:
		pipeline.constants.base_uv_chroma[0] = 0.5f / float(image_info.width);
		pipeline.constants.base_uv_chroma[1] = 1.0f / float(image_info.height);
		break;

	default:
		break;
	}

	image_info.format = chroma_format;

	if (!impl->using_pyro_encoder)
	{
		pipeline.chroma_full = impl->device->create_image(image_info);
		impl->device->set_name(*pipeline.chroma_full, "video-encode-chroma-full-res");
	}

	image_info.width = impl->options.width / 2;
	image_info.height = impl->options.height / 2;

	if (!impl->using_pyro_encoder)
	{
#ifdef HAVE_FFMPEG_VULKAN
		if (impl->hw.get_pix_fmt() != AV_PIX_FMT_VULKAN)
#endif
		{
			pipeline.chroma = impl->device->create_image(image_info);
			impl->device->set_name(*pipeline.chroma, "video-encode-chroma-downsampled");

			aligned_width = (image_info.width + 63) & ~63;
			pipeline.planes[pipeline.num_planes].row_length = aligned_width;
			aligned_width *= 2;

			pipeline.planes[pipeline.num_planes].offset = total_size;
			pipeline.planes[pipeline.num_planes].stride = aligned_width * pixel_size;
			pipeline.num_planes++;
			VkDeviceSize chroma_size = aligned_width * image_info.height * pixel_size;
			total_size += chroma_size;
		}
	}

	pipeline.constants.chroma_dispatch[0] = (image_info.width + 7) / 8;
	pipeline.constants.chroma_dispatch[1] = (image_info.height + 7) / 8;

	if (!impl->using_pyro_encoder)
	{
#ifdef HAVE_FFMPEG_VULKAN
		if (impl->hw.get_pix_fmt() != AV_PIX_FMT_VULKAN)
#endif
		{
			Vulkan::BufferCreateInfo buffer_info = {};
			buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
			buffer_info.domain = Vulkan::BufferDomain::CachedHost;
			buffer_info.size = total_size;
			pipeline.buffer = impl->device->create_buffer(buffer_info);
			impl->device->set_name(*pipeline.buffer, "video-encode-readback");
		}
	}

	return pipeline_ptr;
}

int64_t VideoEncoder::sample_realtime_pts() const
{
	return impl->sample_realtime_pts();
}
}
