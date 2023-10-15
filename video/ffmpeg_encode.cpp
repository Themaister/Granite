/* Copyright (c) 2017-2023 Hans-Kristian Arntzen
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
#include <thread>
#include <cstdlib>
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
	PlaneLayout planes[3] = {};
	unsigned num_planes = 0;

	struct Constants
	{
		float inv_resolution_luma[2];
		float inv_resolution_chroma[2];
		float base_uv_luma[2];
		float base_uv_chroma[2];
		uint32_t luma_dispatch[2];
		uint32_t chroma_dispatch[2];
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

struct VideoEncoder::Impl
{
	Vulkan::Device *device = nullptr;
	bool init(Vulkan::Device *device, const char *path, const Options &options);
	void set_mux_stream_callback(MuxStreamCallback *callback);
	bool encode_frame(const uint8_t *buffer, const PlaneLayout *planes, unsigned num_planes,
	                  int64_t pts, int compensate_audio_us);
	bool encode_frame(AVFrame *hw_frame, int64_t pts, int compensate_audio_us);
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

#ifdef HAVE_GRANITE_AUDIO
	bool encode_audio(int compensate_audio_us);
	bool encode_audio_stream(int compensate_audio_us);
	bool encode_audio_source();
#endif

#ifdef HAVE_FFMPEG_VULKAN_ENCODE
	void submit_process_rgb_vulkan(Vulkan::CommandBufferHandle &cmd, YCbCrPipelineData &pipeline);
#endif
	void submit_process_rgb_readback(Vulkan::CommandBufferHandle &cmd, YCbCrPipelineData &pipeline);
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

bool VideoEncoder::Impl::encode_audio_stream(int compensate_audio_us)
{
	size_t read_avail_frames;
	uint32_t latency_us;
	int ret;

	while (audio_stream->get_buffer_status(read_avail_frames, latency_us) &&
	       read_avail_frames >= size_t(audio.av_frame->nb_samples))
	{
		if ((ret = av_frame_make_writable(audio.av_frame)) < 0)
		{
			LOGE("Failed to make frame writable: %d.\n", ret);
			return false;
		}

		size_t read_count;
		if (audio.av_frame->format == AV_SAMPLE_FMT_FLT)
		{
			read_count = audio_stream->read_frames_interleaved_f32(
					reinterpret_cast<float *>(audio.av_frame->data[0]),
					audio.av_frame->nb_samples, false);
		}
		else if (audio.av_frame->format == AV_SAMPLE_FMT_FLTP)
		{
			read_count = audio_stream->read_frames_deinterleaved_f32(
					reinterpret_cast<float *const *>(audio.av_frame->data),
					audio.av_frame->nb_samples, false);
		}
		else
		{
			LOGE("Unknown sample format.\n");
			read_count = 0;
		}

		if (read_count < size_t(audio.av_frame->nb_samples))
		{
			// Shouldn't happen ...
			LOGW("Short read detected (%zu < %d). Filling with silence.\n", read_count, audio.av_frame->nb_samples);
			if (audio.av_frame->format == AV_SAMPLE_FMT_FLTP)
			{
				for (unsigned c = 0; c < 2; c++)
				{
					memset(audio.av_frame->data[c] + read_count * sizeof(float), 0,
					       (size_t(audio.av_frame->nb_samples) - read_count) * sizeof(float));
				}
			}
			else
			{
				memset(audio.av_frame->data[0] + read_count * sizeof(float) * 2, 0,
				       (size_t(audio.av_frame->nb_samples) - read_count) * sizeof(float) * 2);
			}
		}

		// Crude system for handling drift.
		// Ensure monotonic PTS with maximum 1% clock drift.
		auto absolute_ts = sample_realtime_pts() + compensate_audio_us;
		absolute_ts -= latency_us;

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
	}

	return true;
}

bool VideoEncoder::Impl::encode_audio(int compensate_audio_us)
{
	if (options.realtime && audio_stream)
		return encode_audio_stream(compensate_audio_us);
	else if (!options.realtime && audio_source)
		return encode_audio_source();
	else
		return true;
}
#endif

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

	(void)compensate_audio_us;
#ifdef HAVE_GRANITE_AUDIO
	if (!encode_audio(compensate_audio_us))
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
	if (options.format == VideoEncoder::Format::NV12)
	{
		const auto *src_luma = buffer + planes[0].offset;
		const auto *src_chroma = buffer + planes[1].offset;
		auto *dst_luma = video.av_frame->data[0];
		auto *dst_chroma = video.av_frame->data[1];

		unsigned chroma_width = (options.width >> 1) * 2;
		unsigned chroma_height = options.height >> 1;
		for (unsigned y = 0; y < options.height; y++, dst_luma += video.av_frame->linesize[0], src_luma += planes[0].stride)
			memcpy(dst_luma, src_luma, options.width);
		for (unsigned y = 0; y < chroma_height; y++, dst_chroma += video.av_frame->linesize[1], src_chroma += planes[1].stride)
			memcpy(dst_chroma, src_chroma, chroma_width);
	}

	if (options.realtime)
	{
		int64_t target_pts = av_rescale_q_rnd(pts, {1, AV_TIME_BASE}, video.av_ctx->time_base, AV_ROUND_ZERO);

		if (encode_video_pts)
		{
			int64_t delta = std::abs(target_pts - encode_video_pts);
			if (delta > 8 * video.av_ctx->ticks_per_frame)
			{
				// If we're way off (8 frames), catch up instantly.
				encode_video_pts = target_pts;

				// Force an I-frame here since there is a large discontinuity.
				video.av_frame->pict_type = AV_PICTURE_TYPE_I;
			}
			else if (delta >= video.av_ctx->ticks_per_frame / 4)
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
		video.av_frame->duration = video.av_ctx->ticks_per_frame;
		encode_video_pts += video.av_ctx->ticks_per_frame;
	}
	else
		video.av_frame->pts = encode_video_pts++;

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

	(void)compensate_audio_us;
#ifdef HAVE_GRANITE_AUDIO
	if (!encode_audio(compensate_audio_us))
	{
		LOGE("Failed to encode audio.\n");
		return false;
	}
#endif

	return true;
}

bool VideoEncoder::Impl::drain_packets(CodecStream &stream)
{
	int ret = 0;
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

		if (options.realtime)
			if (&stream == &video)
				stream.av_pkt->duration = video.av_ctx->ticks_per_frame;

		if (av_format_ctx_local)
		{
			auto *pkt_clone = av_packet_clone(stream.av_pkt);
			pkt_clone->pts = stream.av_pkt->pts;
			pkt_clone->dts = stream.av_pkt->dts;
			pkt_clone->duration = stream.av_pkt->duration;
			pkt_clone->stream_index = stream.av_stream_local->index;
			av_packet_rescale_ts(pkt_clone, stream.av_ctx->time_base, stream.av_stream_local->time_base);
			ret = av_interleaved_write_frame(av_format_ctx_local, pkt_clone);
			av_packet_unref(pkt_clone);
			if (ret < 0)
			{
				LOGE("Failed to write packet: %d\n", ret);
				break;
			}
		}

		stream.av_pkt->stream_index = stream.av_stream->index;
		av_packet_rescale_ts(stream.av_pkt, stream.av_ctx->time_base, stream.av_stream->time_base);
		ret = av_interleaved_write_frame(av_format_ctx, stream.av_pkt);
		if (ret < 0)
		{
			LOGE("Failed to write packet: %d\n", ret);
			break;
		}

		if (mux_stream_callback)
			avio_flush(av_format_ctx->pb);
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
			// Don't care about streaming platform limitations, so just use the ideal format.
			codec_id = AV_CODEC_ID_OPUS;
			sample_fmt = AV_SAMPLE_FMT_FLT;
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

	audio.av_stream = avformat_new_stream(av_format_ctx, codec);
	if (!audio.av_stream)
	{
		LOGE("Failed to add new stream.\n");
		return false;
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
		audio.av_ctx->time_base = { 1, 1000000 };
	else
		audio.av_ctx->time_base = { 1, audio.av_ctx->sample_rate };

	if (av_format_ctx->oformat->flags & AVFMT_GLOBALHEADER)
		audio.av_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	if (av_format_ctx_local && (av_format_ctx_local->oformat->flags & AVFMT_GLOBALHEADER))
		audio.av_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	audio.av_stream->id = 1;
	audio.av_stream->time_base = audio.av_ctx->time_base;

	if (options.realtime)
		audio.av_ctx->bit_rate = 256 * 1024;

	int ret = avcodec_open2(audio.av_ctx, codec, nullptr);
	if (ret < 0)
	{
		LOGE("Could not open codec: %d\n", ret);
		return false;
	}

	avcodec_parameters_from_context(audio.av_stream->codecpar, audio.av_ctx);
	if (audio.av_stream_local)
		avcodec_parameters_from_context(audio.av_stream_local->codecpar, audio.av_ctx);

	unsigned samples_per_tick;
	if (!options.realtime && (codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE) != 0)
		samples_per_tick = audio_source->get_frames_per_tick();
	else
		samples_per_tick = audio.av_ctx->frame_size;

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

bool VideoEncoder::Impl::init_video_codec()
{
	const AVCodec *codec = avcodec_find_encoder_by_name(options.encoder);

	if (!codec)
	{
		LOGE("Could not find requested encoder \"%s\".\n", options.encoder);
		return false;
	}

	if (avcodec_get_hw_config(codec, 0) != nullptr)
	{
		if (!hw.init_codec_context(codec, device, nullptr))
		{
			LOGW("Failed to init HW encoder context, falling back to software.\n");
			return false;
		}
	}

	video.av_stream = avformat_new_stream(av_format_ctx, codec);
	if (!video.av_stream)
	{
		LOGE("Failed to add new stream.\n");
		return false;
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

	if (options.realtime)
	{
		video.av_ctx->ticks_per_frame = 16;
		video.av_ctx->time_base = { options.frame_timebase.num, options.frame_timebase.den * video.av_ctx->ticks_per_frame };
		// This seems to be important for NVENC.
		// Need more fine-grained timebase to account for realtime jitter in PTS.
	}
	else
	{
		video.av_ctx->time_base = { options.frame_timebase.num, options.frame_timebase.den };
		video.av_ctx->ticks_per_frame = 1;
	}

	video.av_ctx->color_range = AVCOL_RANGE_MPEG;
	video.av_ctx->colorspace = AVCOL_SPC_BT709;
	video.av_ctx->color_primaries = AVCOL_PRI_BT709;

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

	if (av_format_ctx->oformat->flags & AVFMT_GLOBALHEADER)
		video.av_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	// TODO: Is mismatch a problem?
	if (av_format_ctx_local && av_format_ctx_local->oformat->flags & AVFMT_GLOBALHEADER)
		video.av_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	video.av_stream->id = 0;
	video.av_stream->time_base = video.av_ctx->time_base;

	if (video.av_stream_local)
	{
		video.av_stream_local->id = 0;
		video.av_stream_local->time_base = video.av_ctx->time_base;
	}

	AVDictionary *opts = nullptr;

	bool is_x264 = strcmp(options.encoder, "libx264") == 0;

	if (options.realtime || !is_x264)
	{
		video.av_ctx->bit_rate = options.realtime_options.bitrate_kbits * 1000;
		video.av_ctx->rc_buffer_size = options.realtime_options.vbv_size_kbits * 1000;
		video.av_ctx->rc_max_rate = options.realtime_options.max_bitrate_kbits * 1000;
		video.av_ctx->gop_size = int(options.realtime_options.gop_seconds *
				float(video.av_ctx->framerate.num) / float(video.av_ctx->framerate.den));
		if (video.av_ctx->gop_size == 0)
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
#ifdef HAVE_FFMPEG_VULKAN_ENCODE
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

	video.av_pkt = av_packet_alloc();
	if (!video.av_pkt)
		return false;
	return true;
}

bool VideoEncoder::Impl::init(Vulkan::Device *device_, const char *path, const Options &options_)
{
	device = device_;
	options = options_;

	// For file-less formats like RTMP need to specify muxer format.
	const char *muxer = nullptr;
	if (options.realtime && options.realtime_options.muxer_format)
		muxer = options.realtime_options.muxer_format;

	if (!path && !mux_stream_callback)
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
	if ((ret = avformat_alloc_output_context2(&av_format_ctx, nullptr, muxer, path)) < 0)
	{
		LOGE("Failed to open format context: %d\n", ret);
		return false;
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

	av_dump_format(av_format_ctx, 0, path, 1);
	if (av_format_ctx_local)
		av_dump_format(av_format_ctx_local, 0, options.realtime_options.local_backup_path, 1);

	const auto open_file = [this](AVFormatContext *ctx, const char *encode_path) -> bool
	{
		int retval;

		if (!(ctx->flags & AVFMT_NOFILE))
		{
			if (ctx == av_format_ctx && mux_stream_callback)
			{
				auto *mux_stream_callback_buffer = av_malloc(1024);
				AVIOContext *avio = avio_alloc_context(
						static_cast<unsigned char *>(mux_stream_callback_buffer), 1024, AVIO_FLAG_WRITE,
						this, nullptr, [](void *opaque, uint8_t *buf, int buf_size) -> int
						{
							auto *self = static_cast<VideoEncoder::Impl *>(opaque);
							if (self->mux_stream_callback)
								if (!self->mux_stream_callback->write_stream(buf, buf_size))
									self->mux_stream_callback = nullptr;
							return 0;
						}, nullptr);

				if (!avio)
				{
					LOGE("Could not create AVIO context.\n");
					return false;
				}

				ctx->pb = avio;
			}
			else
			{
				retval = avio_open(&ctx->pb, encode_path, AVIO_FLAG_WRITE);
				if (retval < 0)
				{
					LOGE("Could not open file: %d\n", retval);
					return false;
				}
			}
		}

		if ((retval = avformat_write_header(ctx, nullptr)) < 0)
		{
			LOGE("Failed to write format header: %d\n", retval);
			return false;
		}

		return true;
	};

	if (!open_file(av_format_ctx, path))
	{
		cleanup_format_context();
		return false;
	}

	if (av_format_ctx_local && !open_file(av_format_ctx_local, options.realtime_options.local_backup_path))
	{
		cleanup_format_context();
		return false;
	}

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

#ifdef HAVE_FFMPEG_VULKAN_ENCODE
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

void VideoEncoder::Impl::submit_process_rgb_readback(Vulkan::CommandBufferHandle &cmd,
                                                     Granite::VideoEncoder::YCbCrPipelineData &pipeline)
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

void VideoEncoder::process_rgb(Vulkan::CommandBuffer &cmd, YCbCrPipeline &pipeline_ptr, const Vulkan::ImageView &view)
{
	auto &pipeline = *pipeline_ptr;

	if (pipeline.fence)
		pipeline.fence->wait();
	pipeline.fence.reset();

	if (pipeline.hw_frame)
		av_frame_free(&pipeline.hw_frame);

	Vulkan::ImageViewHandle wrapped_planes[2];
	Vulkan::ImageHandle wrapped_image;

#ifdef HAVE_FFMPEG_VULKAN_ENCODE
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
		cmd.set_sampler(0, 0, Vulkan::StockSampler::LinearClamp);
	}
	else
		cmd.set_texture(0, 0, view, Vulkan::StockSampler::LinearClamp);

	cmd.set_storage_texture(0, 1, wrapped_planes[0] ? *wrapped_planes[0] : pipeline.luma->get_view());
	cmd.set_storage_texture(0, 2, pipeline.chroma_full->get_view());

	struct Push
	{
		uint32_t width, height;
		float base_u, base_v;
		float inv_width, inv_height;
	} push = {};

	push.width = impl->video.av_ctx->width;
	push.height = impl->video.av_ctx->height;
	push.inv_width = pipeline.constants.inv_resolution_luma[0];
	push.inv_height = pipeline.constants.inv_resolution_luma[1];
	push.base_u = pipeline.constants.base_uv_luma[0];
	push.base_v = pipeline.constants.base_uv_luma[1];
	cmd.push_constants(&push, 0, sizeof(push));
	cmd.dispatch(pipeline.constants.luma_dispatch[0], pipeline.constants.luma_dispatch[1], 1);

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
	cmd.dispatch(pipeline.constants.chroma_dispatch[0], pipeline.constants.chroma_dispatch[1], 1);

	if (pipeline.chroma)
	{
		cmd.image_barrier(*pipeline.chroma, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
		                  VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_TRANSFER_READ_BIT);

		cmd.copy_image_to_buffer(*pipeline.buffer, *pipeline.luma, pipeline.planes[0].offset,
		                         {},
		                         {pipeline.luma->get_width(), pipeline.luma->get_height(), 1},
		                         pipeline.planes[0].row_length, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1});

		if (impl->options.format == Format::NV12)
		{
			cmd.copy_image_to_buffer(*pipeline.buffer, *pipeline.chroma, pipeline.planes[1].offset,
			                         {},
			                         {pipeline.chroma->get_width(), pipeline.chroma->get_height(), 1},
			                         pipeline.planes[1].row_length, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1});
		}

		cmd.barrier(VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
		            VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
	}
}

bool VideoEncoder::encode_frame(YCbCrPipeline &pipeline_ptr, int64_t pts, int compensate_audio_us)
{
	auto &pipeline = *pipeline_ptr;
	if (!pipeline.fence)
		return false;

	bool ret;

	if (pipeline.hw_frame)
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

#ifdef HAVE_FFMPEG_VULKAN_ENCODE
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

	auto image_info = Vulkan::ImageCreateInfo::immutable_2d_image(impl->options.width, impl->options.height, VK_FORMAT_R8_UNORM);
	image_info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	image_info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

#ifdef HAVE_FFMPEG_VULKAN_ENCODE
	if (impl->hw.get_pix_fmt() != AV_PIX_FMT_VULKAN)
#endif
	{
		pipeline.luma = impl->device->create_image(image_info);
		impl->device->set_name(*pipeline.luma, "video-encode-luma");
	}

	VkDeviceSize total_size = 0;
	VkDeviceSize aligned_width = (image_info.width + 63) & ~63;
	VkDeviceSize luma_size = aligned_width * image_info.height;

	pipeline.planes[pipeline.num_planes].offset = total_size;
	pipeline.planes[pipeline.num_planes].stride = aligned_width;
	pipeline.planes[pipeline.num_planes].row_length = aligned_width;
	pipeline.num_planes++;
	total_size += luma_size;

	pipeline.constants.inv_resolution_luma[0] = 1.0f / float(image_info.width);
	pipeline.constants.inv_resolution_luma[1] = 1.0f / float(image_info.height);
	pipeline.constants.base_uv_luma[0] = 0.5f / float(image_info.width);
	pipeline.constants.base_uv_luma[1] = 0.5f / float(image_info.height);
	pipeline.constants.luma_dispatch[0] = (image_info.width + 7) / 8;
	pipeline.constants.luma_dispatch[1] = (image_info.height + 7) / 8;

	if (impl->options.format == Format::NV12)
	{
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

		image_info.format = VK_FORMAT_R8G8_UNORM;
		pipeline.chroma_full = impl->device->create_image(image_info);
		impl->device->set_name(*pipeline.chroma_full, "video-encode-chroma-full-res");
		image_info.width = impl->options.width / 2;
		image_info.height = impl->options.height / 2;

#ifdef HAVE_FFMPEG_VULKAN_ENCODE
		if (impl->hw.get_pix_fmt() != AV_PIX_FMT_VULKAN)
#endif
		{
			pipeline.chroma = impl->device->create_image(image_info);
			impl->device->set_name(*pipeline.chroma, "video-encode-chroma-downsampled");

			aligned_width = (image_info.width + 63) & ~63;
			pipeline.planes[pipeline.num_planes].row_length = aligned_width;
			aligned_width *= 2;

			pipeline.planes[pipeline.num_planes].offset = total_size;
			pipeline.planes[pipeline.num_planes].stride = aligned_width;
			pipeline.num_planes++;
			VkDeviceSize chroma_size = aligned_width * image_info.height;
			total_size += chroma_size;
		}

		pipeline.constants.chroma_dispatch[0] = (image_info.width + 7) / 8;
		pipeline.constants.chroma_dispatch[1] = (image_info.height + 7) / 8;
	}

#ifdef HAVE_FFMPEG_VULKAN_ENCODE
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

	return pipeline_ptr;
}

int64_t VideoEncoder::sample_realtime_pts() const
{
	return impl->sample_realtime_pts();
}
}
