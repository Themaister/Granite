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
extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
}

#include "ffmpeg_encode.hpp"
#include "logging.hpp"
#include "math.hpp"
#include "muglm/muglm_impl.hpp"
#include "transforms.hpp"
#include "thread_group.hpp"
#include "timer.hpp"
#include <thread>
#ifdef HAVE_GRANITE_AUDIO
#include "audio_interface.hpp"
#include "dsp/dsp.hpp"
#endif

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

struct VideoEncoder::Impl
{
	Vulkan::Device *device = nullptr;
	bool init(Vulkan::Device *device, const char *path, const Options &options);
	bool encode_frame(const uint8_t *buffer, const PlaneLayout *planes, unsigned num_planes,
	                  int64_t pts, int compensate_audio_us);
	~Impl();

	AVFormatContext *av_format_ctx = nullptr;
	AVFormatContext *av_format_ctx_local = nullptr;
	CodecStream video, audio;
	Options options;
	Audio::DumpBackend *audio_source = nullptr;
	Audio::RecordStream *audio_stream = nullptr;

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

	int64_t sample_realtime_pts() const;
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

		const auto close_fmt_context = [](AVFormatContext *fmt_ctx) {
			if (fmt_ctx)
			{
				av_write_trailer(fmt_ctx);
				if (!(fmt_ctx->flags & AVFMT_NOFILE))
					avio_closep(&fmt_ctx->pb);
				avformat_free_context(fmt_ctx);
			}
		};

		close_fmt_context(av_format_ctx);
		close_fmt_context(av_format_ctx_local);
	}

	free_av_objects(video);
	free_av_objects(audio);
}

VideoEncoder::Impl::~Impl()
{
	drain_codec();
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
		video.av_frame->pts = pts;
	else
		video.av_frame->pts = encode_video_pts++;

	ret = avcodec_send_frame(video.av_ctx, video.av_frame);
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
	if (options.realtime && audio_stream)
	{
		size_t read_avail_frames;
		uint32_t latency_us;
		while (audio_stream->get_buffer_status(read_avail_frames, latency_us) &&
		       read_avail_frames >= size_t(audio.av_frame->nb_samples))
		{
			if ((ret = av_frame_make_writable(audio.av_frame)) < 0)
			{
				LOGE("Failed to make frame writable: %d.\n", ret);
				return false;
			}

			size_t read_count = audio_stream->read_frames_f32(
					reinterpret_cast<float * const *>(audio.av_frame->data),
					audio.av_frame->nb_samples, false);

			if (read_count < size_t(audio.av_frame->nb_samples))
			{
				// Shouldn't happen ...
				LOGW("Short read detected (%zu < %d). Filling with silence.\n", read_count, audio.av_frame->nb_samples);
				for (unsigned c = 0; c < 2; c++)
				{
					memset(audio.av_frame->data[c] + read_count * sizeof(float), 0,
					       (size_t(audio.av_frame->nb_samples) - read_count) * sizeof(float));
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
	}
	else if (!options.realtime && audio_source)
	{
		// Render out audio in the main thread to ensure exact reproducibility across runs.
		// If we don't care about that, we can render audio directly in the thread worker.
		int64_t target_audio_samples = av_rescale_q_rnd(encode_video_pts, video.av_ctx->time_base, audio.av_ctx->time_base, AV_ROUND_UP);
		int64_t to_render = std::max<int64_t>(target_audio_samples - audio_pts, 0);
		audio_buffer_s16.resize(to_render * 2);
		audio_source->drain_interleaved_s16(audio_buffer_s16.data(), to_render);
		audio_pts += to_render;

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

		if (av_format_ctx_local)
		{
			auto *pkt_clone = av_packet_clone(stream.av_pkt);
			pkt_clone->pts = stream.av_pkt->pts;
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
	AVCodecID codec_id;

	// Streaming wants AAC.
	if (options.realtime)
		codec_id = AV_CODEC_ID_AAC;
	else
		codec_id = AV_CODEC_ID_FLAC;

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

	// Just hardcode for what FFmpeg supports. We control which encoders we care about.
	audio.av_ctx->sample_fmt = audio_stream ? AV_SAMPLE_FMT_FLTP : AV_SAMPLE_FMT_S16;

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
	const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_H264);
	if (!codec)
	{
		LOGE("Could not find H.264 encoder.\n");
		return false;
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

	video.av_ctx->framerate = { options.frame_timebase.den, options.frame_timebase.num };

	if (options.realtime)
		video.av_ctx->time_base = { 1, 1000000 };
	else
		video.av_ctx->time_base = { options.frame_timebase.num, options.frame_timebase.den };

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

	if (options.realtime)
	{
		video.av_ctx->bit_rate = options.realtime_options.bitrate_kbits * 1000;
		video.av_ctx->rc_buffer_size = options.realtime_options.vbv_size_kbits * 1000;
		video.av_ctx->rc_max_rate = options.realtime_options.max_bitrate_kbits * 1000;
		video.av_ctx->gop_size = int(options.realtime_options.gop_seconds *
				float(video.av_ctx->framerate.num) / float(video.av_ctx->framerate.den));
		if (video.av_ctx->gop_size == 0)
			video.av_ctx->gop_size = 1;
		if (options.realtime_options.x264_preset)
			av_dict_set(&opts, "preset", options.realtime_options.x264_preset, 0);
		if (options.realtime_options.x264_tune)
			av_dict_set(&opts, "tune", options.realtime_options.x264_tune, 0);
		if (options.realtime_options.threads)
			av_dict_set_int(&opts, "threads", options.realtime_options.threads, 0);
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

	video.av_frame = alloc_video_frame(video.av_ctx->pix_fmt, options.width, options.height);
	if (!video.av_frame)
	{
		LOGE("Failed to allocate AVFrame.\n");
		return false;
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

	const auto open_file = [](AVFormatContext *ctx, const char *encode_path) -> bool
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

bool VideoEncoder::encode_frame(const uint8_t *buffer, const PlaneLayout *planes, unsigned num_planes,
								int64_t pts, int compensate_audio_us)
{
	return impl->encode_frame(buffer, planes, num_planes, pts, compensate_audio_us);
}

void VideoEncoder::process_rgb(Vulkan::CommandBuffer &cmd, YCbCrPipeline &pipeline, const Vulkan::ImageView &view)
{
	cmd.image_barrier(*pipeline.luma, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
					  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
					  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
	cmd.image_barrier(*pipeline.chroma_full, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
	                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
	                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
	cmd.image_barrier(*pipeline.chroma, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
	                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
	                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

	cmd.set_program(pipeline.rgb_to_ycbcr);

	if (Vulkan::format_is_srgb(view.get_format()))
	{
		cmd.set_unorm_texture(0, 0, view);
		cmd.set_sampler(0, 0, Vulkan::StockSampler::LinearClamp);
	}
	else
		cmd.set_texture(0, 0, view, Vulkan::StockSampler::LinearClamp);

	cmd.set_storage_texture(0, 1, pipeline.luma->get_view());
	cmd.set_storage_texture(0, 2, pipeline.chroma_full->get_view());

	struct Push
	{
		uint32_t width, height;
		float base_u, base_v;
		float inv_width, inv_height;
	} push = {};

	push.width = pipeline.luma->get_width();
	push.height = pipeline.luma->get_width();
	push.inv_width = pipeline.constants.inv_resolution_luma[0];
	push.inv_height = pipeline.constants.inv_resolution_luma[1];
	push.base_u = pipeline.constants.base_uv_luma[0];
	push.base_v = pipeline.constants.base_uv_luma[1];
	cmd.push_constants(&push, 0, sizeof(push));
	cmd.dispatch(pipeline.constants.luma_dispatch[0], pipeline.constants.luma_dispatch[1], 1);

	cmd.image_barrier(*pipeline.luma, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
	                  VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_TRANSFER_READ_BIT);
	cmd.image_barrier(*pipeline.chroma_full, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
					  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

	cmd.set_program(pipeline.chroma_downsample);
	cmd.set_texture(0, 0, pipeline.chroma_full->get_view(), Vulkan::StockSampler::LinearClamp);
	cmd.set_storage_texture(0, 1, pipeline.chroma->get_view());

	push.inv_width = pipeline.constants.inv_resolution_chroma[0];
	push.inv_height = pipeline.constants.inv_resolution_chroma[1];
	push.base_u = pipeline.constants.base_uv_chroma[0];
	push.base_v = pipeline.constants.base_uv_chroma[1];
	cmd.push_constants(&push, 0, sizeof(push));
	cmd.dispatch(pipeline.constants.chroma_dispatch[0], pipeline.constants.chroma_dispatch[1], 1);

	cmd.image_barrier(*pipeline.chroma, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
	                  VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_TRANSFER_READ_BIT);

	cmd.copy_image_to_buffer(*pipeline.buffer, *pipeline.luma, pipeline.planes[0].offset,
	                         {},
	                         { pipeline.luma->get_width(), pipeline.luma->get_height(), 1 },
	                         pipeline.planes[0].row_length, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 });

	if (impl->options.format == Format::NV12)
	{
		cmd.copy_image_to_buffer(*pipeline.buffer, *pipeline.chroma, pipeline.planes[1].offset,
		                         {},
		                         { pipeline.chroma->get_width(), pipeline.chroma->get_height(), 1 },
		                         pipeline.planes[1].row_length, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 });
	}

	cmd.barrier(VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
}

bool VideoEncoder::encode_frame(YCbCrPipeline &pipeline, int64_t pts, int compensate_audio_us)
{
	if (!pipeline.fence)
		return false;
	pipeline.fence->wait();

	auto *buf = static_cast<const uint8_t *>(impl->device->map_host_buffer(*pipeline.buffer, Vulkan::MEMORY_ACCESS_READ_BIT));
	bool ret = encode_frame(buf, pipeline.planes, pipeline.num_planes, pts, compensate_audio_us);
	impl->device->unmap_host_buffer(*pipeline.buffer, Vulkan::MEMORY_ACCESS_READ_BIT);
	return ret;
}

VideoEncoder::YCbCrPipeline VideoEncoder::create_ycbcr_pipeline() const
{
	YCbCrPipeline pipeline;

	auto *rgb_to_yuv = impl->device->get_shader_manager().register_compute("builtin://shaders/util/rgb_to_yuv.comp");
	pipeline.rgb_to_ycbcr = rgb_to_yuv->register_variant({})->get_program();

	auto *chroma_downsample = impl->device->get_shader_manager().register_compute("builtin://shaders/util/chroma_downsample.comp");
	pipeline.chroma_downsample = chroma_downsample->register_variant({})->get_program();

	auto image_info = Vulkan::ImageCreateInfo::immutable_2d_image(impl->options.width, impl->options.height, VK_FORMAT_R8_UNORM);
	image_info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	image_info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	pipeline.luma = impl->device->create_image(image_info);
	impl->device->set_name(*pipeline.luma, "video-encode-luma");

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

		pipeline.constants.chroma_dispatch[0] = (image_info.width + 7) / 8;
		pipeline.constants.chroma_dispatch[1] = (image_info.height + 7) / 8;
	}

	Vulkan::BufferCreateInfo buffer_info = {};
	buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	buffer_info.domain = Vulkan::BufferDomain::CachedHost;
	buffer_info.size = total_size;
	pipeline.buffer = impl->device->create_buffer(buffer_info);
	impl->device->set_name(*pipeline.buffer, "video-encode-readback");

	return pipeline;
}

int64_t VideoEncoder::sample_realtime_pts() const
{
	return impl->sample_realtime_pts();
}
}