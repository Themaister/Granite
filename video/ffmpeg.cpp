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

namespace Granite
{
constexpr unsigned NumFrames = 4;
struct VideoEncoder::Impl
{
	Vulkan::Device *device = nullptr;
	bool init(Vulkan::Device *device, const char *path, const Options &options);
	bool push_frame(const Vulkan::Image &image, VkImageLayout layout,
	                Vulkan::CommandBuffer::Type type, const Vulkan::Semaphore &semaphore,
	                Vulkan::Semaphore &release_semaphore);
	~Impl();

	AVStream *av_stream = nullptr;
	AVFrame *av_frame = nullptr;
	AVFormatContext *av_format_ctx = nullptr;
	AVCodecContext *av_codec_ctx = nullptr;
	AVPacket *av_packet = nullptr;
	SwsContext *sws_ctx = nullptr;
	Options options;

	void drain_codec();
	AVFrame *alloc_frame(AVPixelFormat pix_fmt, unsigned width, unsigned height);

	struct Frame
	{
		Vulkan::BufferHandle buffer;
		Vulkan::Fence fence;
		ThreadLatch latch;
		int stride = 0;
	};
	Frame frames[NumFrames];
	unsigned frame_index = 0;
	std::thread thr;

	bool enqueue_buffer_readback(
			const Vulkan::Image &image, VkImageLayout layout,
			Vulkan::CommandBuffer::Type type,
			const Vulkan::Semaphore &semaphore,
			Vulkan::Semaphore &release_semaphore);

	bool drain_packets();
	void drain();
	void thread_main();
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

void VideoEncoder::Impl::drain_codec()
{
	if (av_format_ctx)
	{
		if (av_packet)
		{
			int ret = avcodec_send_frame(av_codec_ctx, nullptr);
			if (ret < 0)
				LOGE("Failed to send packet to codec: %d\n", ret);
			else if (!drain_packets())
				LOGE("Failed to drain codec of packets.\n");
		}

		av_write_trailer(av_format_ctx);
		if (!(av_format_ctx->flags & AVFMT_NOFILE))
			avio_closep(&av_format_ctx->pb);
		avformat_free_context(av_format_ctx);
	}

	if (av_frame)
		av_frame_free(&av_frame);
	if (sws_ctx)
		sws_freeContext(sws_ctx);
	if (av_packet)
		av_packet_free(&av_packet);
}

VideoEncoder::Impl::~Impl()
{
	for (auto &frame : frames)
		frame.latch.kill_latch();

	if (thr.joinable())
		thr.join();

	drain_codec();
	if (av_codec_ctx)
		avcodec_free_context(&av_codec_ctx);
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
	frame.latch.set_latch();
	return true;
}

bool VideoEncoder::Impl::drain_packets()
{
	int ret = 0;
	while (ret >= 0)
	{
		ret = avcodec_receive_packet(av_codec_ctx, av_packet);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
			break;
		else if (ret < 0)
		{
			LOGE("Error encoding frame: %d\n", ret);
			break;
		}

		av_packet_rescale_ts(av_packet, av_codec_ctx->time_base, av_stream->time_base);
		av_packet->stream_index = av_stream->index;
		ret = av_interleaved_write_frame(av_format_ctx, av_packet);
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
	int64_t pts = 0;

	for (;;)
	{
		index = (index + 1) % NumFrames;
		auto &frame = frames[index];
		if (!frame.latch.wait_latch_set())
			break;

		int ret;
		if ((ret = av_frame_make_writable(av_frame)) < 0)
		{
			LOGE("Failed to make frame writable: %d.\n", ret);
			frame.latch.kill_latch();
			break;
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

		sws_scale(sws_ctx, src_slices, linesizes,
		          0, int(options.height),
		          av_frame->data, av_frame->linesize);
		av_frame->pts = pts++;

		device->unmap_host_buffer(*frame.buffer, Vulkan::MEMORY_ACCESS_READ_BIT);
		frame.latch.clear_latch();

		ret = avcodec_send_frame(av_codec_ctx, av_frame);
		if (ret < 0)
		{
			LOGE("Failed to send packet to codec: %d\n", ret);
			frame.latch.kill_latch();
			break;
		}

		if (!drain_packets())
		{
			frame.latch.kill_latch();
			break;
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

bool VideoEncoder::Impl::init(Vulkan::Device *device_, const char *path, const Options &options_)
{
	device = device_;
	options = options_;

	AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_H264);
	if (!codec)
	{
		LOGE("Could not find H.264 encoder.\n");
		return false;
	}

	av_codec_ctx = avcodec_alloc_context3(codec);
	if (!av_codec_ctx)
	{
		LOGE("Failed to allocate codec context.\n");
		return false;
	}

	av_codec_ctx->width = options.width;
	av_codec_ctx->height = options.height;
	av_codec_ctx->pix_fmt = AV_PIX_FMT_YUV444P;
	av_codec_ctx->framerate = { options.frame_timebase.den, options.frame_timebase.num };
	av_codec_ctx->time_base = { options.frame_timebase.num, options.frame_timebase.den };

	int ret;
	if ((ret = avformat_alloc_output_context2(&av_format_ctx, nullptr, nullptr, path)) < 0)
	{
		LOGE("Failed to open format context: %d\n", ret);
		return false;
	}

	av_stream = avformat_new_stream(av_format_ctx, codec);
	if (!av_stream)
	{
		LOGE("Failed to add new stream.\n");
		return false;
	}

	av_stream->id = 0;
	av_stream->time_base = av_codec_ctx->time_base;
	if (av_format_ctx->oformat->flags & AVFMT_GLOBALHEADER)
		av_codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	AVDictionary *opts = nullptr;
	av_dict_set_int(&opts, "crf", 25, 0);
	av_dict_set(&opts, "preset", "fast", 0);
	ret = avcodec_open2(av_codec_ctx, codec, &opts);
	av_dict_free(&opts);

	if (ret < 0)
	{
		LOGE("Could not open codec: %d\n", ret);
		return false;
	}

	avcodec_parameters_from_context(av_stream->codecpar, av_codec_ctx);

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

	av_frame = alloc_frame(av_codec_ctx->pix_fmt, options.width, options.height);
	if (!av_frame)
	{
		LOGE("Failed to allocate AVFrame.\n");
		return false;
	}

	sws_ctx = sws_getContext(options.width, options.height,
	                         AV_PIX_FMT_RGB0,
	                         options.width, options.height,
	                         AV_PIX_FMT_YUV444P, SWS_POINT,
	                         nullptr, nullptr, nullptr);

	av_packet = av_packet_alloc();
	thr = std::thread(&Impl::thread_main, this);

	return true;
}

AVFrame *VideoEncoder::Impl::alloc_frame(AVPixelFormat pix_fmt, unsigned width, unsigned height)
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