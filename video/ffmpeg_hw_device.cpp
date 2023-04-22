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
	Vulkan::Device *device = nullptr;
	const AVCodec *cached_av_codec = nullptr;

	~Impl()
	{
		if (hw_device)
			av_buffer_unref(&hw_device);
	}

	bool init_hw_device(const AVCodec *av_codec)
	{
#ifdef HAVE_FFMPEG_VULKAN
		bool use_vulkan = false;
		const char *env = getenv("GRANITE_FFMPEG_VULKAN");
		if (env && strtol(env, nullptr, 0) != 0)
			use_vulkan = true;

		if (use_vulkan)
		{
			if (!device->get_device_features().sampler_ycbcr_conversion_features.samplerYcbcrConversion)
			{
				LOGW("Sampler YCbCr conversion not supported, disabling Vulkan interop.\n");
				use_vulkan = false;
			}
		}
#endif

		for (int i = 0; !hw_device; i++)
		{
			const AVCodecHWConfig *config = avcodec_get_hw_config(av_codec, i);
			if (!config)
				break;

#ifdef HAVE_FFMPEG_VULKAN
			if (config->device_type == AV_HWDEVICE_TYPE_VULKAN && !use_vulkan)
				continue;
			if (config->device_type != AV_HWDEVICE_TYPE_VULKAN && use_vulkan)
				continue;
#endif

			if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) != 0)
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

					vk->queue_family_encode_index = -1;
					vk->nb_encode_queues = 0;

					vk->lock_queue = [](AVHWDeviceContext *ctx, int, int) {
						auto *self = static_cast<Impl *>(ctx->user_opaque);
						self->device->external_queue_lock();
					};

					vk->unlock_queue = [](AVHWDeviceContext *ctx, int, int) {
						auto *self = static_cast<Impl *>(ctx->user_opaque);
						self->device->external_queue_unlock();
					};

					if (av_hwdevice_ctx_init(hw_dev) >= 0)
					{
						LOGI("Created custom Vulkan HW decoder.\n");
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
						LOGI("Created HW decoder: %s.\n", av_hwdevice_get_type_name(config->device_type));
						hw_config = config;
						hw_device = hw_dev;
						break;
					}
				}
			}
		}

		return hw_device != nullptr;
	}

	bool init_codec_context(const AVCodec *av_codec, Vulkan::Device *device_, AVCodecContext *av_ctx)
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

		if (!init_hw_device(av_codec))
			return false;

		av_ctx->get_format = get_pixel_format;
		av_ctx->hw_device_ctx = av_buffer_ref(hw_device);
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
						AV_PIX_FMT_VULKAN, &ctx->hw_frames_ctx) < 0)
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

bool FFmpegHWDevice::init_codec_context(const AVCodec *codec, Vulkan::Device *device, AVCodecContext *ctx)
{
	if (!impl)
		impl.reset(new Impl);
	return impl->init_codec_context(codec, device, ctx);
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

void FFmpegHWDevice::reset()
{
	impl.reset();
}
}