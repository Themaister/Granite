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

#define __STDC_LIMIT_MACROS 1
extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
}

#include "ffmpeg.hpp"
#include "logging.hpp"
#include "thread_latch.hpp"
#include <condition_variable>
#include <mutex>
#include <thread>
#ifdef HAVE_GRANITE_AUDIO
#include "audio_interface.hpp"
#endif

namespace Granite
{
static constexpr unsigned NumFrames = 4;

struct CodecStream
{
	AVStream *av_stream = nullptr;
	AVFrame *av_frame = nullptr;
	AVCodecContext *av_ctx = nullptr;
	AVPacket *av_pkt = nullptr;
	SwsContext *sws_ctx = nullptr;
};

struct VideoEncoder::Impl
{
	Vulkan::Device *device = nullptr;
	bool init(Vulkan::Device *device, const char *path, const Options &options);
	bool push_frame(const Vulkan::Image &image, VkImageLayout layout,
	                Vulkan::CommandBuffer::Type type, const Vulkan::Semaphore &semaphore,
	                Vulkan::Semaphore &release_semaphore);
	~Impl();

	AVFormatContext *av_format_ctx = nullptr;
	CodecStream video, audio;
	Options options;
	Audio::DumpBackend *audio_source = nullptr;

	void drain_codec();
	AVFrame *alloc_video_frame(AVPixelFormat pix_fmt, unsigned width, unsigned height);
	AVFrame *alloc_audio_frame(AVSampleFormat samp_format, uint64_t channel_layout,
	                           unsigned sample_rate, unsigned sample_count);

	struct Frame
	{
		Vulkan::BufferHandle buffer;
		Vulkan::Fence fence;
		ThreadLatch latch;
		int stride = 0;
		std::vector<int16_t> audio_buffer;
	};
	Frame frames[NumFrames];
	unsigned frame_index = 0;
	std::thread thr;

	bool enqueue_buffer_readback(
			const Vulkan::Image &image, VkImageLayout layout,
			Vulkan::CommandBuffer::Type type,
			const Vulkan::Semaphore &semaphore,
			Vulkan::Semaphore &release_semaphore);

	bool drain_packets(CodecStream &stream);
	void drain();
	void thread_main();

	bool init_video_codec();
	bool init_audio_codec();

	int64_t audio_pts = 0;
	int64_t video_pts = 0;
};

void VideoEncoder::Impl::drain()
{
	for (auto &frame : frames)
	{
		frame.latch.wait_latch_cleared();
		frame.buffer.reset();
		frame.fence.reset();
	}
}

static void free_av_objects(CodecStream &stream)
{
	if (stream.av_frame)
		av_frame_free(&stream.av_frame);
	if (stream.sws_ctx)
		sws_freeContext(stream.sws_ctx);
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

		av_write_trailer(av_format_ctx);
		if (!(av_format_ctx->flags & AVFMT_NOFILE))
			avio_closep(&av_format_ctx->pb);
		if (av_format_ctx)
			avformat_free_context(av_format_ctx);
	}

	free_av_objects(video);
	free_av_objects(audio);
}

VideoEncoder::Impl::~Impl()
{
	for (auto &frame : frames)
		frame.latch.kill_latch();

	if (thr.joinable())
		thr.join();

	drain_codec();
}

bool VideoEncoder::Impl::enqueue_buffer_readback(
		const Vulkan::Image &image, VkImageLayout layout,
		Vulkan::CommandBuffer::Type type,
		const Vulkan::Semaphore &semaphore,
		Vulkan::Semaphore &release_semaphore)
{
	frame_index = (frame_index + 1) % NumFrames;
	auto &frame = frames[frame_index];

	if (!frame.latch.wait_latch_cleared())
	{
		LOGE("Encoding thread died ...\n");
		return false;
	}

	unsigned width = image.get_width();
	unsigned height = image.get_height();
	unsigned aligned_width = (width + 63) & ~63;
	unsigned pix_size = Vulkan::TextureFormatLayout::format_block_size(image.get_format(), VK_IMAGE_ASPECT_COLOR_BIT);
	frame.stride = int(pix_size * aligned_width);

	Vulkan::BufferCreateInfo buf;
	buf.size = aligned_width * height * pix_size;
	buf.domain = Vulkan::BufferDomain::CachedHost;
	buf.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	if (!frame.buffer || frame.buffer->get_create_info().size != buf.size)
		frame.buffer = device->create_buffer(buf);

	Vulkan::OwnershipTransferInfo transfer_info = {};
	transfer_info.old_queue = type;
	transfer_info.new_queue = Vulkan::CommandBuffer::Type::AsyncTransfer;
	transfer_info.old_image_layout = layout;
	transfer_info.new_image_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	transfer_info.dst_pipeline_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
	transfer_info.dst_access = VK_ACCESS_TRANSFER_READ_BIT;

	auto transfer_cmd = Vulkan::request_command_buffer_with_ownership_transfer(
			*device, image, transfer_info, semaphore);

	transfer_cmd->copy_image_to_buffer(*frame.buffer, image, 0, {}, { width, height, 1, },
	                                   aligned_width, height,
	                                   { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 });

	transfer_cmd->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
						  VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);

	device->submit(transfer_cmd, &frame.fence, 1, &release_semaphore);

	// Render out audio in the main thread to ensure exact reproducibility across runs.
	// If we don't care about that, we can render audio directly in the thread worker.
#ifdef HAVE_GRANITE_AUDIO
	video_pts++;
	if (audio_source)
	{
		int64_t target_audio_samples = av_rescale_q_rnd(video_pts, video.av_ctx->time_base, audio.av_ctx->time_base, AV_ROUND_UP);
		int64_t to_render = std::max<int64_t>(target_audio_samples - audio_pts, 0);
		frame.audio_buffer.resize(to_render * 2);
		audio_source->drain_interleaved_s16(frame.audio_buffer.data(), to_render);
		audio_pts += to_render;
	}
#endif

	frame.latch.set_latch();
	return true;
}

bool VideoEncoder::Impl::drain_packets(CodecStream &stream)
{
	int ret = 0;
	while (ret >= 0)
	{
		ret = avcodec_receive_packet(stream.av_ctx, stream.av_pkt);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
			break;
		else if (ret < 0)
		{
			LOGE("Error encoding frame: %d\n", ret);
			break;
		}

		av_packet_rescale_ts(stream.av_pkt, stream.av_ctx->time_base, stream.av_stream->time_base);
		stream.av_pkt->stream_index = stream.av_stream->index;
		ret = av_interleaved_write_frame(av_format_ctx, stream.av_pkt);
		if (ret < 0)
		{
			LOGE("Failed to write packet: %d\n", ret);
			break;
		}
	}

	return ret == 0 || ret == AVERROR_EOF || ret == AVERROR(EAGAIN);
}

void VideoEncoder::Impl::thread_main()
{
	unsigned index = 0;
	int64_t encode_video_pts = 0;
	int64_t encode_audio_pts = 0;
	int current_audio_frames = 0;
	int ret;

	for (;;)
	{
		index = (index + 1) % NumFrames;
		auto &frame = frames[index];
		if (!frame.latch.wait_latch_set())
			return;

		if (audio.av_pkt)
		{
			for (size_t i = 0, n = frame.audio_buffer.size() / 2; i < n; )
			{
				int to_copy = std::min<int>(int(n - i), audio.av_frame->nb_samples - current_audio_frames);

				if (current_audio_frames == 0)
				{
					if ((ret = av_frame_make_writable(audio.av_frame)) < 0)
					{
						LOGE("Failed to make frame writable: %d.\n", ret);
						frame.latch.kill_latch();
						return;
					}
				}

				memcpy(reinterpret_cast<int16_t *>(audio.av_frame->data[0]) + 2 * current_audio_frames,
				       frame.audio_buffer.data() + 2 * i, to_copy * 2 * sizeof(int16_t));

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
						frame.latch.kill_latch();
						return;
					}

					if (!drain_packets(audio))
					{
						frame.latch.kill_latch();
						return;
					}
				}

				i += to_copy;
			}
		}

		if ((ret = av_frame_make_writable(video.av_frame)) < 0)
		{
			LOGE("Failed to make frame writable: %d.\n", ret);
			frame.latch.kill_latch();
			return;
		}

		if (frame.fence)
		{
			frame.fence->wait();
			frame.fence.reset();
		}

		const uint8_t *src_slices[4] = {
			static_cast<const uint8_t *>(device->map_host_buffer(*frame.buffer, Vulkan::MEMORY_ACCESS_READ_BIT)),
		};

		const int linesizes[4] = { frame.stride };

		sws_scale(video.sws_ctx, src_slices, linesizes,
		          0, int(options.height),
		          video.av_frame->data, video.av_frame->linesize);
		video.av_frame->pts = encode_video_pts++;

		device->unmap_host_buffer(*frame.buffer, Vulkan::MEMORY_ACCESS_READ_BIT);

		frame.latch.clear_latch();

		ret = avcodec_send_frame(video.av_ctx, video.av_frame);
		if (ret < 0)
		{
			LOGE("Failed to send packet to codec: %d\n", ret);
			frame.latch.kill_latch();
			return;
		}

		if (!drain_packets(video))
		{
			frame.latch.kill_latch();
			return;
		}
	}
}

bool VideoEncoder::Impl::push_frame(const Vulkan::Image &image, VkImageLayout layout,
                                    Vulkan::CommandBuffer::Type type,
                                    const Vulkan::Semaphore &semaphore,
                                    Vulkan::Semaphore &release_semaphore)
{
	if (image.get_width() != options.width || image.get_height() != options.height)
		return false;

	return enqueue_buffer_readback(image, layout, type, semaphore, release_semaphore);
}

void VideoEncoder::set_audio_source(Audio::DumpBackend *backend)
{
	impl->audio_source = backend;
}

bool VideoEncoder::Impl::init_audio_codec()
{
#ifdef HAVE_GRANITE_AUDIO
	AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_FLAC);
	if (!codec)
	{
		LOGE("Could not find FLAC encoder.\n");
		return false;
	}

	audio.av_stream = avformat_new_stream(av_format_ctx, codec);
	if (!audio.av_stream)
	{
		LOGE("Failed to add new stream.\n");
		return false;
	}

	audio.av_ctx = avcodec_alloc_context3(codec);
	if (!audio.av_ctx)
	{
		LOGE("Failed to allocate codec context.\n");
		return false;
	}

	audio.av_ctx->sample_fmt = AV_SAMPLE_FMT_S16;
	audio.av_ctx->sample_rate = int(audio_source->get_sample_rate());
	audio.av_ctx->channels = 2;
	audio.av_ctx->channel_layout = AV_CH_LAYOUT_STEREO;
	audio.av_ctx->time_base = { 1, audio.av_ctx->sample_rate };

	if (av_format_ctx->oformat->flags & AVFMT_GLOBALHEADER)
		audio.av_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	audio.av_stream->id = 1;
	audio.av_stream->time_base = audio.av_ctx->time_base;

	int ret = avcodec_open2(audio.av_ctx, codec, nullptr);
	if (ret < 0)
	{
		LOGE("Could not open codec: %d\n", ret);
		return false;
	}

	avcodec_parameters_from_context(audio.av_stream->codecpar, audio.av_ctx);

	unsigned samples_per_tick;
	if ((codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE) != 0)
		samples_per_tick = audio_source->get_frames_per_tick();
	else
		samples_per_tick = audio.av_ctx->frame_size;

	audio.av_frame = alloc_audio_frame(audio.av_ctx->sample_fmt, audio.av_ctx->channel_layout,
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
	AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_H264);
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

	video.av_ctx = avcodec_alloc_context3(codec);
	if (!video.av_ctx)
	{
		LOGE("Failed to allocate codec context.\n");
		return false;
	}

	video.av_ctx->width = options.width;
	video.av_ctx->height = options.height;
	video.av_ctx->pix_fmt = AV_PIX_FMT_YUV444P;
	video.av_ctx->framerate = { options.frame_timebase.den, options.frame_timebase.num };
	video.av_ctx->time_base = { options.frame_timebase.num, options.frame_timebase.den };

	if (av_format_ctx->oformat->flags & AVFMT_GLOBALHEADER)
		video.av_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	video.av_stream->id = 0;
	video.av_stream->time_base = video.av_ctx->time_base;

	AVDictionary *opts = nullptr;
	av_dict_set_int(&opts, "crf", 18, 0);
	av_dict_set(&opts, "preset", "fast", 0);
	int ret = avcodec_open2(video.av_ctx, codec, &opts);
	av_dict_free(&opts);

	if (ret < 0)
	{
		LOGE("Could not open codec: %d\n", ret);
		return false;
	}

	avcodec_parameters_from_context(video.av_stream->codecpar, video.av_ctx);

	video.av_frame = alloc_video_frame(video.av_ctx->pix_fmt, options.width, options.height);
	if (!video.av_frame)
	{
		LOGE("Failed to allocate AVFrame.\n");
		return false;
	}

	video.sws_ctx = sws_getContext(options.width, options.height,
	                               AV_PIX_FMT_RGB0,
	                               options.width, options.height,
	                               AV_PIX_FMT_YUV444P, SWS_POINT,
	                               nullptr, nullptr, nullptr);
	if (!video.sws_ctx)
		return false;

	video.av_pkt = av_packet_alloc();
	if (!video.av_pkt)
		return false;
	return true;
}

bool VideoEncoder::Impl::init(Vulkan::Device *device_, const char *path, const Options &options_)
{
	device = device_;
	options = options_;

	int ret;
	if ((ret = avformat_alloc_output_context2(&av_format_ctx, nullptr, nullptr, path)) < 0)
	{
		LOGE("Failed to open format context: %d\n", ret);
		return false;
	}

	if (!init_video_codec())
		return false;
	if (audio_source && !init_audio_codec())
		return false;

	av_dump_format(av_format_ctx, 0, path, 1);

	if (!(av_format_ctx->flags & AVFMT_NOFILE))
	{
		ret = avio_open(&av_format_ctx->pb, path, AVIO_FLAG_WRITE);
		if (ret < 0)
		{
			LOGE("Could not open file: %d\n", ret);
			return false;
		}
	}

	if ((ret = avformat_write_header(av_format_ctx, nullptr)) < 0)
	{
		LOGE("Failed to write format header: %d\n", ret);
		return false;
	}

	thr = std::thread(&Impl::thread_main, this);
	return true;
}

AVFrame *VideoEncoder::Impl::alloc_audio_frame(
		AVSampleFormat samp_format, uint64_t channel_layout,
		unsigned sample_rate, unsigned sample_count)
{
	AVFrame *frame = av_frame_alloc();
	if (!frame)
		return nullptr;

	frame->channel_layout = channel_layout;
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

bool VideoEncoder::push_frame(const Vulkan::Image &image, VkImageLayout layout, Vulkan::CommandBuffer::Type type,
                              const Vulkan::Semaphore &semaphore,
							  Vulkan::Semaphore &release_semaphore)
{
	return impl->push_frame(image, layout, type, semaphore, release_semaphore);
}

void VideoEncoder::drain()
{
	impl->drain();
}
}