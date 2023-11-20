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
#include "ffmpeg_hw_device.hpp"
#include "logging.hpp"
#include "device.hpp"

extern "C"
{
#include <libavcodec/avcodec.h>
#ifdef HAVE_FFMPEG_VULKAN
#include <libavutil/hwcontext_vulkan.h>
#endif
}

namespace Granite
{
static AVPixelFormat get_pixel_format(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts);

struct FFmpegHWDevice::Impl
{
	const AVCodecHWConfig *hw_config = nullptr;
	AVBufferRef *hw_device = nullptr;
	AVBufferRef *frame_ctx = nullptr;
	Vulkan::Device *device = nullptr;
	const AVCodec *cached_av_codec = nullptr;

#ifdef HAVE_FFMPEG_VULKAN_ENCODE
	VkVideoProfileInfoKHR profile_info = { VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR };
	VkVideoProfileListInfoKHR profile_list_info = { VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR };
	VkVideoEncodeH264ProfileInfoEXT h264_encode = { VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_PROFILE_INFO_EXT };
#endif

	~Impl()
	{
		if (frame_ctx)
			av_buffer_unref(&frame_ctx);
		if (hw_device)
			av_buffer_unref(&hw_device);
	}

	void init_hw_device_ctx(const AVCodecHWConfig *config)
	{
#ifdef HAVE_FFMPEG_VULKAN
		if (config->device_type == AV_HWDEVICE_TYPE_VULKAN)
		{
			AVBufferRef *hw_dev = av_hwdevice_ctx_alloc(config->device_type);
			auto *hwctx = reinterpret_cast<AVHWDeviceContext *>(hw_dev->data);
			auto *vk = static_cast<AVVulkanDeviceContext *>(hwctx->hwctx);

			hwctx->user_opaque = this;

			vk->get_proc_addr = Vulkan::Context::get_instance_proc_addr();
			vk->inst = device->get_instance();
			vk->act_dev = device->get_device();
			vk->phys_dev = device->get_physical_device();
			vk->device_features = *device->get_device_features().pdf2;
			vk->enabled_inst_extensions = device->get_device_features().instance_extensions;
			vk->nb_enabled_inst_extensions = int(device->get_device_features().num_instance_extensions);
			vk->enabled_dev_extensions = device->get_device_features().device_extensions;
			vk->nb_enabled_dev_extensions = int(device->get_device_features().num_device_extensions);

			auto &q = device->get_queue_info();

			vk->queue_family_index = int(q.family_indices[Vulkan::QUEUE_INDEX_GRAPHICS]);
			vk->queue_family_comp_index = int(q.family_indices[Vulkan::QUEUE_INDEX_COMPUTE]);
			vk->queue_family_tx_index = int(q.family_indices[Vulkan::QUEUE_INDEX_TRANSFER]);
			vk->queue_family_decode_index = int(q.family_indices[Vulkan::QUEUE_INDEX_VIDEO_DECODE]);

			vk->nb_graphics_queues = int(q.counts[Vulkan::QUEUE_INDEX_GRAPHICS]);
			vk->nb_comp_queues = int(q.counts[Vulkan::QUEUE_INDEX_COMPUTE]);
			vk->nb_tx_queues = int(q.counts[Vulkan::QUEUE_INDEX_TRANSFER]);
			vk->nb_decode_queues = int(q.counts[Vulkan::QUEUE_INDEX_VIDEO_DECODE]);

#ifdef VK_ENABLE_BETA_EXTENSIONS
			vk->queue_family_encode_index = int(q.family_indices[Vulkan::QUEUE_INDEX_VIDEO_ENCODE]);
			vk->nb_encode_queues = int(q.counts[Vulkan::QUEUE_INDEX_VIDEO_ENCODE]);
#else
			vk->queue_family_encode_index = -1;
			vk->nb_encode_queues = 0;
#endif

			vk->lock_queue = [](AVHWDeviceContext *ctx, uint32_t, uint32_t) {
				auto *self = static_cast<Impl *>(ctx->user_opaque);
				self->device->external_queue_lock();
			};

			vk->unlock_queue = [](AVHWDeviceContext *ctx, uint32_t, uint32_t) {
				auto *self = static_cast<Impl *>(ctx->user_opaque);
				self->device->external_queue_unlock();
			};

			if (av_hwdevice_ctx_init(hw_dev) >= 0)
			{
				LOGI("Created custom Vulkan FFmpeg HW device.\n");
				hw_config = config;
				hw_device = hw_dev;
			}
			else
				av_buffer_unref(&hw_dev);
		}
		else
#endif
		{
			AVBufferRef *hw_dev = nullptr;
			if (av_hwdevice_ctx_create(&hw_dev, config->device_type, nullptr, nullptr, 0) == 0)
			{
				LOGI("Created FFmpeg HW device: %s.\n", av_hwdevice_get_type_name(config->device_type));
				hw_config = config;
				hw_device = hw_dev;
			}
		}
	}

	bool init_hw_device(const AVCodec *av_codec, const char *type)
	{
#ifdef HAVE_FFMPEG_VULKAN
		bool use_vulkan = false;
		const char *env = getenv("GRANITE_FFMPEG_VULKAN");
		if (env && strtol(env, nullptr, 0) != 0)
			use_vulkan = true;
#endif

		for (int i = 0; !hw_device; i++)
		{
			const AVCodecHWConfig *config = avcodec_get_hw_config(av_codec, i);
			if (!config)
				break;
			if (config->device_type == AV_HWDEVICE_TYPE_NONE)
				continue;

#ifdef HAVE_FFMPEG_VULKAN
			if (config->device_type == AV_HWDEVICE_TYPE_VULKAN && !use_vulkan)
			{
				LOGI("Found Vulkan HW device, but Vulkan was not enabled in device.\n");
				continue;
			}
#endif

			if (type)
			{
				const char *hwdevice_name = av_hwdevice_get_type_name(config->device_type);
				LOGI("Found HW device type: %s\n", hwdevice_name);
				if (strcmp(type, hwdevice_name) != 0)
					continue;
			}

			if ((config->methods & (AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX | AV_CODEC_HW_CONFIG_METHOD_HW_FRAMES_CTX)) != 0)
				init_hw_device_ctx(config);
		}

		return hw_device != nullptr;
	}

	bool init_frame_context(AVCodecContext *av_ctx, unsigned width, unsigned height, AVPixelFormat sw_format)
	{
		if ((hw_config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_FRAMES_CTX) == 0)
			return false;

		auto *frames = av_hwframe_ctx_alloc(hw_device);
		if (!frames)
			return false;

		auto *ctx = reinterpret_cast<AVHWFramesContext *>(frames->data);
		ctx->format = hw_config->pix_fmt;
		ctx->width = width;
		ctx->height = height;
		ctx->sw_format = sw_format;

#ifdef HAVE_FFMPEG_VULKAN_ENCODE
		if (ctx->format == AV_PIX_FMT_VULKAN)
		{
			auto *vk = static_cast<AVVulkanFramesContext *>(ctx->hwctx);
			vk->img_flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_EXTENDED_USAGE_BIT;
			// XXX: FFmpeg header type bug.
			vk->usage = VkImageUsageFlagBits(
					vk->usage | VK_IMAGE_USAGE_STORAGE_BIT |
					VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR);

			h264_encode.stdProfileIdc = STD_VIDEO_H264_PROFILE_IDC_HIGH;

			profile_info.videoCodecOperation = VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_EXT;
			profile_info.chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
			profile_info.lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
			profile_info.chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;
			profile_info.pNext = &h264_encode;

			profile_list_info.pProfiles = &profile_info;
			profile_list_info.profileCount = 1;
			vk->create_pnext = &profile_list_info;
		}
#endif

		if (av_hwframe_ctx_init(frames) != 0)
		{
			LOGE("Failed to initialize HW frame context.\n");
			av_buffer_unref(&frames);
			return false;
		}

		frame_ctx = frames;
		av_ctx->hw_frames_ctx = av_buffer_ref(frame_ctx);
		return true;
	}

	bool init_codec_context(const AVCodec *av_codec, Vulkan::Device *device_,
	                        AVCodecContext *av_ctx, const char *type)
	{
		if (device && (device != device_ || av_codec != cached_av_codec))
		{
			if (hw_device)
			{
				av_buffer_unref(&hw_device);
				hw_device = nullptr;
			}
		}

		device = device_;
		cached_av_codec = av_codec;

		if (!init_hw_device(av_codec, type))
			return false;

		if (av_ctx && (hw_config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) != 0)
		{
			av_ctx->get_format = get_pixel_format;
			av_ctx->hw_device_ctx = av_buffer_ref(hw_device);
		}
		return true;
	}
};

static AVPixelFormat get_pixel_format(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts)
{
	auto *self = static_cast<FFmpegHWDevice *>(ctx->opaque);
	while (*pix_fmts != AV_PIX_FMT_NONE)
	{
		if (*pix_fmts == self->impl->hw_config->pix_fmt)
		{
#ifdef HAVE_FFMPEG_VULKAN
			if (self->impl->hw_config->pix_fmt == AV_PIX_FMT_VULKAN)
			{
				if (avcodec_get_hw_frames_parameters(
						ctx, ctx->hw_device_ctx,
						self->impl->hw_config->pix_fmt, &ctx->hw_frames_ctx) < 0)
				{
					LOGE("Failed to get HW frames parameters.\n");
					return AV_PIX_FMT_NONE;
				}

				auto *frames = reinterpret_cast<AVHWFramesContext *>(ctx->hw_frames_ctx->data);
				auto *vk = static_cast<AVVulkanFramesContext *>(frames->hwctx);
				// We take views of individual planes if we don't get a clean YCbCr sampler, need this.
				vk->img_flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;

				if (av_hwframe_ctx_init(ctx->hw_frames_ctx) < 0)
				{
					LOGE("Failed to initialize HW frames context.\n");
					av_buffer_unref(&ctx->hw_frames_ctx);
					return AV_PIX_FMT_NONE;
				}
			}
#endif
			return *pix_fmts;
		}
		pix_fmts++;
	}

	return AV_PIX_FMT_NONE;
}

FFmpegHWDevice::FFmpegHWDevice()
{
}

FFmpegHWDevice::~FFmpegHWDevice()
{
}

bool FFmpegHWDevice::init_codec_context(const AVCodec *codec, Vulkan::Device *device,
                                        AVCodecContext *ctx, const char *type)
{
	if (!impl)
		impl.reset(new Impl);
	return impl->init_codec_context(codec, device, ctx, type);
}

bool FFmpegHWDevice::init_frame_context(AVCodecContext *ctx,
                                        unsigned width, unsigned height, int sw_pixel_format)
{
	if (!impl || !impl->hw_device || !impl->hw_config)
		return false;

	return impl->init_frame_context(ctx, width, height, AVPixelFormat(sw_pixel_format));
}

int FFmpegHWDevice::get_hw_device_type() const
{
	if (!impl || !impl->hw_device || !impl->hw_config)
		return AV_HWDEVICE_TYPE_NONE;

	return impl->hw_config->device_type;
}

int FFmpegHWDevice::get_pix_fmt() const
{
	if (!impl || !impl->hw_device || !impl->hw_config)
		return AV_PIX_FMT_NONE;

	return impl->hw_config->pix_fmt;
}

int FFmpegHWDevice::get_sw_pix_fmt() const
{
	if (!impl || !impl->frame_ctx)
		return AV_PIX_FMT_NONE;

	auto *frames = reinterpret_cast<AVHWFramesContext *>(impl->frame_ctx->data);
	return frames->sw_format;
}

void FFmpegHWDevice::reset()
{
	impl.reset();
}
}
