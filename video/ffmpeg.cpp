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

#define __STDC_LIMIT_MACROS 1
extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include "ffmpeg.hpp"
#include "logging.hpp"
#include "thread_latch.hpp"
#include "math.hpp"
#include "muglm/muglm_impl.hpp"
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
	AVFrame *alloc_audio_frame(AVSampleFormat samp_format, AVChannelLayout channel_layout,
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
	transfer_info.dst_pipeline_stage = VK_PIPELINE_STAGE_2_COPY_BIT;
	transfer_info.dst_access = VK_ACCESS_TRANSFER_READ_BIT;

	auto transfer_cmd = Vulkan::request_command_buffer_with_ownership_transfer(
			*device, image, transfer_info, semaphore);

	transfer_cmd->copy_image_to_buffer(*frame.buffer, image, 0, {}, { width, height, 1, },
	                                   aligned_width, height,
	                                   { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 });

	transfer_cmd->barrier(VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
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

		av_packet_rescale_ts(stream.av_pkt, stream.av_ctx->time_base, stream.av_stream->time_base);
		stream.av_pkt->stream_index = stream.av_stream->index;
		ret = av_interleaved_write_frame(av_format_ctx, stream.av_pkt);
		if (ret < 0)
		{
			LOGE("Failed to write packet: %d\n", ret);
			break;
		}
	}

	return ret == AVERROR_EOF || ret == AVERROR(EAGAIN);
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
	const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_FLAC);
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
	audio.av_ctx->ch_layout = AV_CHANNEL_LAYOUT_STEREO;
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

static constexpr unsigned NumDecodeFrames = 8;

struct VideoDecoder::Impl
{
	Vulkan::Device *device = nullptr;
	AVFormatContext *av_format_ctx = nullptr;
	AVPacket *av_pkt = nullptr;
	CodecStream video;

	struct DecodedImage
	{
		Vulkan::ImageHandle rgb_image;
		Vulkan::ImageHandle planes[3];

		// Current buffer state can be deduced.
		// release_order == 0, acquired by application.
		// release_order != 0 && !sem_to_client, not yet rendered to.
		// Rendering can select lowest release order when rendering video.
		// release_order != 0 && sem_to_client, ready for application to render to.
		// When sem_to_client is completely filled there is nothing for the rendering
		// thread to do, and it should block.
		// It can be kicked by audio rendering.
		Vulkan::Semaphore sem_to_client;
		Vulkan::Semaphore sem_from_client;
		uint64_t release_order = 1;

		double pts = 0.0;
	};
	DecodedImage video_queue[NumDecodeFrames];
	uint64_t release_timestamps = 1;
	double last_acquired_pts = -1.0;
	bool is_eof = false;
	bool is_flushing = false;

	VkFormat plane_formats[3] = {};
	unsigned plane_subsample_log2[3] = {};
	unsigned num_planes = 0;
	int staging_image_size = 0;

	bool init(Granite::Audio::Mixer *mixer, const char *path);
	bool eof();

	bool begin_device_context(Vulkan::Device *device);
	void end_device_context();
	bool play();

	double get_estimated_audio_playback_timestamp();
	void set_target_video_timestamp(double ts);

	bool acquire_video_frame(VideoFrame &frame);
	void release_video_frame(unsigned index, Vulkan::Semaphore sem);

	int find_acquire_video_frame_locked();

	bool decode_video_packet(AVPacket *pkt);

	bool iterate();

	int find_decode_video_frame();
	bool process_video_frame();

	~Impl();
};

int VideoDecoder::Impl::find_decode_video_frame()
{
	int best_index = -1;
	for (unsigned i = 0; i < NumDecodeFrames; i++)
	{
		if (video_queue[i].release_order != 0 &&
		    !video_queue[i].sem_to_client &&
		    (best_index < 0 || video_queue[i].release_order < video_queue[best_index].release_order))
		{
			best_index = int(i);
		}
	}

	// We have no choice but to trample on a frame we already decoded.
	// This can happen if audio is running ahead, and we need to start catching up.
	// For this reason, we should consume the produced image with lowest PTS.
	if (best_index < 0)
	{
		for (unsigned i = 0; i < NumDecodeFrames; i++)
		{
			if (video_queue[i].release_order != 0 &&
			    (best_index < 0 || video_queue[i].release_order < video_queue[best_index].release_order))
			{
				best_index = int(i);
			}
		}
	}

	// This shouldn't happen. Applications cannot acquire a ton of images like this.
	assert(best_index >= 0);

	auto &img = video_queue[best_index];

	if (!img.rgb_image)
	{
		auto info = Vulkan::ImageCreateInfo::immutable_2d_image(video.av_ctx->width,
		                                                        video.av_ctx->height,
		                                                        VK_FORMAT_R8G8B8A8_SRGB);
		info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		info.flags = VK_IMAGE_CREATE_EXTENDED_USAGE_BIT;
		info.misc = Vulkan::IMAGE_MISC_CONCURRENT_QUEUE_GRAPHICS_BIT |
		            Vulkan::IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_COMPUTE_BIT |
		            Vulkan::IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_GRAPHICS_BIT |
		            Vulkan::IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_TRANSFER_BIT |
		            Vulkan::IMAGE_MISC_MUTABLE_SRGB_BIT;
		img.rgb_image = device->create_image(info);

		info.misc &= ~Vulkan::IMAGE_MISC_MUTABLE_SRGB_BIT;

		for (unsigned plane = 0; plane < num_planes; plane++)
		{
			info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
			info.flags = 0;
			info.format = plane_formats[plane];
			info.width = video.av_ctx->width >> plane_subsample_log2[plane];
			info.height = video.av_ctx->height >> plane_subsample_log2[plane];
			img.planes[plane] = device->create_image(info);
		}
	}

	return best_index;
}

bool VideoDecoder::Impl::init(Granite::Audio::Mixer *, const char *path)
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

	int ret;
	if ((ret = av_find_best_stream(av_format_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0)) < 0)
	{
		LOGE("Failed to find best stream.\n");
		return false;
	}

	video.av_stream = av_format_ctx->streams[ret];
	const AVCodec *codec = avcodec_find_decoder(video.av_stream->codecpar->codec_id);
	if (!codec)
	{
		LOGE("Failed to find codec.\n");
		return false;
	}

	video.av_ctx = avcodec_alloc_context3(codec);
	if (!video.av_ctx)
	{
		LOGE("Failed to allocate codec context.\n");
		return false;
	}

	if (avcodec_parameters_to_context(video.av_ctx, video.av_stream->codecpar) < 0)
	{
		LOGE("Failed to copy codec parameters.\n");
		return false;
	}

	if (avcodec_open2(video.av_ctx, codec, nullptr) < 0)
	{
		LOGE("Failed to open codec.\n");
		return false;
	}

	// TODO: Is there a way to make this data driven?
	switch (video.av_ctx->pix_fmt)
	{
	case AV_PIX_FMT_YUV444P:
	case AV_PIX_FMT_YUV420P:
		plane_formats[0] = VK_FORMAT_R8_UNORM;
		plane_formats[1] = VK_FORMAT_R8_UNORM;
		plane_formats[2] = VK_FORMAT_R8_UNORM;
		plane_subsample_log2[1] = video.av_ctx->pix_fmt == AV_PIX_FMT_YUV420P ? 1 : 0;
		plane_subsample_log2[2] = video.av_ctx->pix_fmt == AV_PIX_FMT_YUV420P ? 1 : 0;
		num_planes = 3;
		break;

	case AV_PIX_FMT_NV12:
	case AV_PIX_FMT_NV21:
		plane_formats[0] = VK_FORMAT_R8_UNORM;
		plane_formats[1] = VK_FORMAT_R8G8_UNORM;
		num_planes = 2;
		plane_subsample_log2[1] = 1;
		break;

	default:
		LOGE("Unrecognized pixel format.\n");
		return false;
	}

	staging_image_size = av_image_get_buffer_size(video.av_ctx->pix_fmt,
												  video.av_ctx->width,
												  video.av_ctx->height,
												  1);

	if (!(video.av_frame = av_frame_alloc()))
	{
		LOGE("Failed to allocate frame.\n");
		return false;
	}

	if (!(av_pkt = av_packet_alloc()))
	{
		LOGE("Failed to allocate packet.\n");
		return false;
	}

	return true;
}

int VideoDecoder::Impl::find_acquire_video_frame_locked()
{
	// Want frame with PTS > last_acquired_pts and sem_to_client is set.
	int best_index = -1;
	for (unsigned i = 0; i < NumDecodeFrames; i++)
	{
		if (video_queue[i].sem_to_client && video_queue[i].pts > last_acquired_pts &&
		    (best_index < 0 || (video_queue[i].pts < video_queue[best_index].pts)))
		{
			best_index = int(i);
		}
	}

	return best_index;
}

bool VideoDecoder::Impl::process_video_frame()
{
	int frame = find_decode_video_frame();
	if (frame < 0)
		return false;

	auto &img = video_queue[frame];

	img.pts = double(video.av_frame->pts);

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
		if (plane_formats[i] == VK_FORMAT_R8G8_UNORM)
			byte_width *= 2;

		av_image_copy_plane(buf, int(img.planes[i]->get_width()),
		                    video.av_frame->data[i], video.av_frame->linesize[i],
		                    byte_width, int(img.planes[i]->get_height()));
	}

	for (unsigned i = 0; i < num_planes; i++)
	{
		cmd->image_barrier(*img.planes[i],
		                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		                   VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
		                   VK_PIPELINE_STAGE_NONE, 0);
	}

	img.sem_to_client.reset();
	Vulkan::Semaphore transfer_to_compute;

	if (img.sem_from_client)
	{
		device->add_wait_semaphore(Vulkan::CommandBuffer::Type::AsyncTransfer,
		                           std::move(img.sem_from_client),
		                           VK_PIPELINE_STAGE_2_COPY_BIT,
		                           true);
		img.sem_from_client = {};
	}
	device->submit(cmd, nullptr, 1, &transfer_to_compute);
	device->add_wait_semaphore(Vulkan::CommandBuffer::Type::AsyncCompute,
	                           std::move(transfer_to_compute),
	                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
	                           true);

	cmd = device->request_command_buffer(Vulkan::CommandBuffer::Type::AsyncCompute);
	cmd->set_unorm_storage_texture(0, 0, img.rgb_image->get_view());
	for (unsigned i = 0; i < num_planes; i++)
	{
		cmd->set_texture(0, 1 + i, img.planes[i]->get_view(),
		                 i == 0 ? Vulkan::StockSampler::NearestClamp : Vulkan::StockSampler::LinearClamp);
	}
	for (unsigned i = num_planes; i < 3; i++)
		cmd->set_texture(0, 1 + i, img.planes[0]->get_view(), Vulkan::StockSampler::NearestClamp);
	cmd->set_program("builtin://shaders/util/yuv_to_rgb.comp");

	*cmd->allocate_typed_constant_data<mat4>(1, 0, 1) = mat4(1.0f);

	struct Push
	{
		uvec2 resolution;
		vec2 inv_resolution;
		vec2 chroma_siting;
	} push = {};

	push.resolution = uvec2(img.rgb_image->get_width(), img.rgb_image->get_height());
	push.inv_resolution = vec2(1.0f / float(img.rgb_image->get_width()),
	                           1.0f / float(img.rgb_image->get_height()));
	push.chroma_siting = vec2(0.5f) * push.inv_resolution;
	cmd->push_constants(&push, 0, sizeof(push));

	cmd->image_barrier(*img.rgb_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
	                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
	                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
	cmd->dispatch((push.resolution.x + 7) / 8, (push.resolution.y + 7) / 8, 1);
	cmd->image_barrier(*img.rgb_image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
	                   VK_PIPELINE_STAGE_NONE, 0);
	device->submit(cmd, nullptr, 1, &img.sem_to_client);
	return true;
}

bool VideoDecoder::Impl::decode_video_packet(AVPacket *pkt)
{
	int ret;
	if ((ret = avcodec_receive_frame(video.av_ctx, video.av_frame)) >= 0)
	{
		process_video_frame();
		av_frame_unref(video.av_frame);
		return true;
	}

	if (pkt)
	{
		ret = avcodec_send_packet(video.av_ctx, pkt);
		if (ret < 0)
		{
			LOGE("Failed to send packet.\n");
			return false;
		}

		if ((ret = avcodec_receive_frame(video.av_ctx, video.av_frame)) >= 0)
		{
			process_video_frame();
			av_frame_unref(video.av_frame);
		}
	}

	return ret >= 0 || ret == AVERROR(EAGAIN);
}

bool VideoDecoder::Impl::iterate()
{
	if (is_eof)
		return false;

	if (!is_flushing)
	{
		int ret;
		while ((ret = av_read_frame(av_format_ctx, av_pkt)) >= 0)
		{
			if (av_pkt->stream_index == video.av_stream->index)
				if (!decode_video_packet(av_pkt))
					is_eof = true;

			av_packet_unref(av_pkt);
			break;
		}

		if (ret == AVERROR_EOF)
		{
			avcodec_send_packet(video.av_ctx, nullptr);
			is_flushing = true;
		}
		else if (ret < 0)
			return false;
	}

	if (is_flushing && !decode_video_packet(nullptr))
		is_eof = true;

	return true;
}

bool VideoDecoder::Impl::acquire_video_frame(VideoFrame &frame)
{
	// Poll the video queue for new frames.
	int index = find_acquire_video_frame_locked();
	while (index < 0)
	{
		// TODO: Wait on progress from thread.
		if (!iterate())
			return false;

		index = find_acquire_video_frame_locked();
	}

	// Now we can return a frame.
	frame.sem.reset();
	std::swap(frame.sem, video_queue[index].sem_to_client);
	video_queue[index].release_order = 0;
	frame.view = &video_queue[index].rgb_image->get_view();
	frame.index = index;
	frame.pts = video_queue[index].pts;
	last_acquired_pts = frame.pts;

	return true;
}

void VideoDecoder::Impl::release_video_frame(unsigned index, Vulkan::Semaphore sem)
{
	// Need to hold lock here.
	assert(video_queue[index].release_order == 0);
	video_queue[index].sem_from_client = std::move(sem);
	video_queue[index].release_order = ++release_timestamps;
}

void VideoDecoder::Impl::set_target_video_timestamp(double ts)
{
}

bool VideoDecoder::Impl::begin_device_context(Vulkan::Device *device_)
{
	device = device_;
	return true;
}

double VideoDecoder::Impl::get_estimated_audio_playback_timestamp()
{
	return -1.0;
}

void VideoDecoder::Impl::end_device_context()
{
	device = nullptr;
}

bool VideoDecoder::Impl::eof()
{
	return false;
}

bool VideoDecoder::Impl::play()
{
	return false;
}

VideoDecoder::Impl::~Impl()
{
	free_av_objects(video);
	if (av_format_ctx)
	{
		if (!(av_format_ctx->flags & AVFMT_NOFILE))
			avio_closep(&av_format_ctx->pb);
		avformat_free_context(av_format_ctx);
	}
	if (av_pkt)
		av_packet_free(&av_pkt);
}

VideoDecoder::VideoDecoder()
{
	impl.reset(new Impl);
}

VideoDecoder::~VideoDecoder()
{
}

bool VideoDecoder::init(Granite::Audio::Mixer *mixer, const char *path)
{
	return impl->init(mixer, path);
}

bool VideoDecoder::begin_device_context(Vulkan::Device *device)
{
	return impl->begin_device_context(device);
}

void VideoDecoder::end_device_context()
{
	impl->end_device_context();
}

bool VideoDecoder::play()
{
	return impl->play();
}

double VideoDecoder::get_estimated_audio_playback_timestamp()
{
	return impl->get_estimated_audio_playback_timestamp();
}

bool VideoDecoder::eof()
{
	return impl->eof();
}

bool VideoDecoder::acquire_video_frame(VideoFrame &frame)
{
	return impl->acquire_video_frame(frame);
}

void VideoDecoder::release_video_frame(unsigned index, Vulkan::Semaphore sem)
{
	impl->release_video_frame(index, std::move(sem));
}

void VideoDecoder::set_target_video_timestamp(double ts)
{
	impl->set_target_video_timestamp(ts);
}
}