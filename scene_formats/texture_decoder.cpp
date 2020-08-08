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

#include "texture_decoder.hpp"
#include "global_managers.hpp"
#include "logging.hpp"

namespace Granite
{
static VkFormat compressed_format_to_decoded_format(VkFormat format)
{
	switch (format)
	{
	case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
	case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
	case VK_FORMAT_BC2_SRGB_BLOCK:
	case VK_FORMAT_BC3_SRGB_BLOCK:
	case VK_FORMAT_BC7_SRGB_BLOCK:
		return VK_FORMAT_R8G8B8A8_SRGB;

	case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
	case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
	case VK_FORMAT_BC2_UNORM_BLOCK:
	case VK_FORMAT_BC3_UNORM_BLOCK:
	case VK_FORMAT_BC7_UNORM_BLOCK:
		return VK_FORMAT_R8G8B8A8_UNORM;

	case VK_FORMAT_BC4_UNORM_BLOCK:
		return VK_FORMAT_R8_UNORM;
	case VK_FORMAT_BC5_UNORM_BLOCK:
		return VK_FORMAT_R8G8_UNORM;

	case VK_FORMAT_BC4_SNORM_BLOCK:
	case VK_FORMAT_BC5_SNORM_BLOCK:
	case VK_FORMAT_EAC_R11G11_SNORM_BLOCK:
	case VK_FORMAT_EAC_R11_SNORM_BLOCK:
		LOGE("SNORM formats are not supported.\n");
		return VK_FORMAT_UNDEFINED;

	case VK_FORMAT_BC6H_SFLOAT_BLOCK:
	case VK_FORMAT_BC6H_UFLOAT_BLOCK:
		return VK_FORMAT_R16G16B16A16_SFLOAT;

	case VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK:
	case VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK:
	case VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK:
		return VK_FORMAT_R8G8B8A8_SRGB;

	case VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK:
	case VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK:
	case VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK:
		return VK_FORMAT_R8G8B8A8_UNORM;

	case VK_FORMAT_EAC_R11_UNORM_BLOCK:
		return VK_FORMAT_R16_SFLOAT;
	case VK_FORMAT_EAC_R11G11_UNORM_BLOCK:
		return VK_FORMAT_R16G16_SFLOAT;

	case VK_FORMAT_ASTC_4x4_UNORM_BLOCK:
	case VK_FORMAT_ASTC_5x4_UNORM_BLOCK:
	case VK_FORMAT_ASTC_5x5_UNORM_BLOCK:
	case VK_FORMAT_ASTC_6x5_UNORM_BLOCK:
	case VK_FORMAT_ASTC_6x6_UNORM_BLOCK:
	case VK_FORMAT_ASTC_8x5_UNORM_BLOCK:
	case VK_FORMAT_ASTC_8x6_UNORM_BLOCK:
	case VK_FORMAT_ASTC_8x8_UNORM_BLOCK:
	case VK_FORMAT_ASTC_10x5_UNORM_BLOCK:
	case VK_FORMAT_ASTC_10x6_UNORM_BLOCK:
	case VK_FORMAT_ASTC_10x8_UNORM_BLOCK:
	case VK_FORMAT_ASTC_10x10_UNORM_BLOCK:
	case VK_FORMAT_ASTC_12x10_UNORM_BLOCK:
	case VK_FORMAT_ASTC_12x12_UNORM_BLOCK:
		return VK_FORMAT_R8G8B8A8_UNORM;

	case VK_FORMAT_ASTC_4x4_SRGB_BLOCK:
	case VK_FORMAT_ASTC_5x4_SRGB_BLOCK:
	case VK_FORMAT_ASTC_5x5_SRGB_BLOCK:
	case VK_FORMAT_ASTC_6x5_SRGB_BLOCK:
	case VK_FORMAT_ASTC_6x6_SRGB_BLOCK:
	case VK_FORMAT_ASTC_8x5_SRGB_BLOCK:
	case VK_FORMAT_ASTC_8x6_SRGB_BLOCK:
	case VK_FORMAT_ASTC_8x8_SRGB_BLOCK:
	case VK_FORMAT_ASTC_10x5_SRGB_BLOCK:
	case VK_FORMAT_ASTC_10x6_SRGB_BLOCK:
	case VK_FORMAT_ASTC_10x8_SRGB_BLOCK:
	case VK_FORMAT_ASTC_10x10_SRGB_BLOCK:
	case VK_FORMAT_ASTC_12x10_SRGB_BLOCK:
	case VK_FORMAT_ASTC_12x12_SRGB_BLOCK:
		return VK_FORMAT_R8G8B8A8_SRGB;

	default:
		return VK_FORMAT_UNDEFINED;
	}
}

static VkFormat compressed_format_to_payload_format(VkFormat format)
{
	auto block_size = Vulkan::TextureFormatLayout::format_block_size(format, VK_IMAGE_ASPECT_COLOR_BIT);

	if (block_size == 4)
		return VK_FORMAT_R32_UINT;
	else if (block_size == 8)
		return VK_FORMAT_R32G32_UINT;
	else if (block_size == 16)
		return VK_FORMAT_R32G32B32A32_UINT;
	else
		return VK_FORMAT_UNDEFINED;
}

static VkFormat to_storage_format(VkFormat format)
{
	switch (format)
	{
	case VK_FORMAT_R8G8B8A8_SRGB:
	case VK_FORMAT_R8G8B8A8_UNORM:
		return VK_FORMAT_R8G8B8A8_UINT;

	case VK_FORMAT_R8_UNORM:
		return VK_FORMAT_R8_UINT;
	case VK_FORMAT_R8G8_UNORM:
		return VK_FORMAT_R8G8_UINT;

	case VK_FORMAT_R16_SFLOAT:
	case VK_FORMAT_R16G16_SFLOAT:
	case VK_FORMAT_R16G16B16A16_SFLOAT:
		return format;

	default:
		return VK_FORMAT_UNDEFINED;
	}
}

static bool set_compute_decoder(Vulkan::CommandBuffer &cmd, VkFormat format)
{
	switch (format)
	{
	case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
	case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
	case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
	case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
	case VK_FORMAT_BC2_SRGB_BLOCK:
	case VK_FORMAT_BC2_UNORM_BLOCK:
	case VK_FORMAT_BC3_SRGB_BLOCK:
	case VK_FORMAT_BC3_UNORM_BLOCK:
		cmd.set_program("builtin://shaders/decode/s3tc.comp");
		break;

	case VK_FORMAT_BC4_UNORM_BLOCK:
	case VK_FORMAT_BC5_UNORM_BLOCK:
		cmd.set_program("builtin://shaders/decode/rgtc.comp");
		break;

	case VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK:
	case VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK:
	case VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK:
	case VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK:
	case VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK:
	case VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK:
		cmd.set_program("builtin://shaders/decode/etc2.comp");
		break;

	case VK_FORMAT_EAC_R11_UNORM_BLOCK:
	case VK_FORMAT_EAC_R11G11_UNORM_BLOCK:
		cmd.set_program("builtin://shaders/decode/eac.comp");
		break;

	default:
		return false;
	}

	return true;
}

static void dispatch_kernel_eac(Vulkan::CommandBuffer &cmd, uint32_t width, uint32_t height, VkFormat format)
{
	struct Push
	{
		uint32_t width, height;
	} push;

	push.width = width;
	push.height = height;
	cmd.push_constants(&push, 0, sizeof(push));

	cmd.set_specialization_constant_mask(1);
	cmd.set_specialization_constant(0, uint32_t(format == VK_FORMAT_EAC_R11G11_UNORM_BLOCK ? 2 : 1));

	width = (width + 7) / 8;
	height = (height + 7) / 8;
	cmd.dispatch(width, height, 1);
}

static void dispatch_kernel_etc2(Vulkan::CommandBuffer &cmd, uint32_t width, uint32_t height, VkFormat format)
{
	struct Push
	{
		uint32_t width, height;
	} push;

	push.width = width;
	push.height = height;
	cmd.push_constants(&push, 0, sizeof(push));

	cmd.set_specialization_constant_mask(1);
	switch (format)
	{
	case VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK:
	case VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK:
		cmd.set_specialization_constant(0, uint32_t(0));
		break;

	case VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK:
	case VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK:
		cmd.set_specialization_constant(0, uint32_t(1));
		break;

	case VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK:
	case VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK:
		cmd.set_specialization_constant(0, uint32_t(8));
		break;

	default:
		break;
	}

	width = (width + 7) / 8;
	height = (height + 7) / 8;
	cmd.dispatch(width, height, 1);
}

static void dispatch_kernel_rgtc(Vulkan::CommandBuffer &cmd, uint32_t width, uint32_t height, VkFormat format)
{
	struct Push
	{
		uint32_t width, height;
	} push;

	push.width = width;
	push.height = height;
	cmd.push_constants(&push, 0, sizeof(push));

	cmd.set_specialization_constant_mask(1);
	cmd.set_specialization_constant(0, uint32_t(format == VK_FORMAT_BC5_UNORM_BLOCK));

	width = (width + 7) / 8;
	height = (height + 7) / 8;
	cmd.dispatch(width, height, 1);
}

static void dispatch_kernel_s3tc(Vulkan::CommandBuffer &cmd, uint32_t width, uint32_t height, VkFormat format)
{
	struct Push
	{
		uint32_t width, height;
	} push;

	push.width = width;
	push.height = height;
	cmd.push_constants(&push, 0, sizeof(push));

	cmd.set_specialization_constant_mask(3);

	switch (format)
	{
	case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
	case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
		cmd.set_specialization_constant(0, uint32_t(0));
		break;

	default:
		cmd.set_specialization_constant(0, uint32_t(1));
		break;
	}

	switch (format)
	{
	case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
	case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
	case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
	case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
		cmd.set_specialization_constant(1, uint32_t(1));
		break;

	case VK_FORMAT_BC2_UNORM_BLOCK:
	case VK_FORMAT_BC2_SRGB_BLOCK:
		cmd.set_specialization_constant(1, uint32_t(2));
		break;

	case VK_FORMAT_BC3_UNORM_BLOCK:
	case VK_FORMAT_BC3_SRGB_BLOCK:
		cmd.set_specialization_constant(1, uint32_t(3));
		break;

	default:
		break;
	}

	width = (width + 7) / 8;
	height = (height + 7) / 8;
	cmd.dispatch(width, height, 1);
}

static void dispatch_kernel(Vulkan::CommandBuffer &cmd, uint32_t width, uint32_t height, VkFormat format)
{
	switch (format)
	{
	case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
	case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
	case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
	case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
	case VK_FORMAT_BC2_SRGB_BLOCK:
	case VK_FORMAT_BC2_UNORM_BLOCK:
	case VK_FORMAT_BC3_SRGB_BLOCK:
	case VK_FORMAT_BC3_UNORM_BLOCK:
		dispatch_kernel_s3tc(cmd, width, height, format);
		break;

	case VK_FORMAT_BC4_UNORM_BLOCK:
	case VK_FORMAT_BC5_UNORM_BLOCK:
		dispatch_kernel_rgtc(cmd, width, height, format);
		break;

	case VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK:
	case VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK:
	case VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK:
	case VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK:
	case VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK:
	case VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK:
		dispatch_kernel_etc2(cmd, width, height, format);
		break;

	case VK_FORMAT_EAC_R11_UNORM_BLOCK:
	case VK_FORMAT_EAC_R11G11_UNORM_BLOCK:
		dispatch_kernel_eac(cmd, width, height, format);
		break;

	default:
		break;
	}
}

Vulkan::ImageHandle decode_compressed_image(Vulkan::CommandBuffer &cmd, const Vulkan::TextureFormatLayout &layout,
                                            const VkComponentMapping &swizzle)
{
	auto &device = cmd.get_device();

	// For EXTENDED_USAGE_BIT.
	if (!device.get_device_features().supports_maintenance_2)
		return {};

	uint32_t block_width, block_height;
	Vulkan::TextureFormatLayout::format_block_dim(layout.get_format(), block_width, block_height);
	if (block_width == 1 || block_height == 1)
	{
		LOGE("Not a compressed format.\n");
		return {};
	}

	auto image_info = Vulkan::ImageCreateInfo::immutable_image(layout);
	image_info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	image_info.format = compressed_format_to_decoded_format(layout.get_format());
	image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
	                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	image_info.flags = VK_IMAGE_CREATE_EXTENDED_USAGE_BIT | VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
	image_info.swizzle = swizzle;
	if (image_info.format == VK_FORMAT_UNDEFINED)
		return {};
	auto decoded_image = device.create_image(image_info);

	auto staging = device.create_image_staging_buffer(layout);
	for (auto &blit : staging.blits)
	{
		blit.bufferRowLength = (blit.bufferRowLength + block_width - 1) / block_width;
		blit.bufferImageHeight = (blit.bufferImageHeight + block_height - 1) / block_height;
		blit.imageExtent.width = (blit.imageExtent.width + block_width - 1) / block_width;
		blit.imageExtent.height = (blit.imageExtent.height + block_height - 1) / block_height;
	}

	image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
	image_info.format = compressed_format_to_payload_format(layout.get_format());
	if (image_info.format == VK_FORMAT_UNDEFINED)
		return {};
	image_info.swizzle = { VK_COMPONENT_SWIZZLE_R,
		                   VK_COMPONENT_SWIZZLE_G,
		                   VK_COMPONENT_SWIZZLE_B,
		                   VK_COMPONENT_SWIZZLE_A };
	image_info.misc = Vulkan::IMAGE_MISC_CONCURRENT_QUEUE_GRAPHICS_BIT |
	                  Vulkan::IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_GRAPHICS_BIT |
	                  Vulkan::IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_COMPUTE_BIT;
	image_info.width = (image_info.width + block_width - 1) / block_width;
	image_info.height = (image_info.height + block_height - 1) / block_height;
	image_info.initial_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	auto uploaded_image = device.create_image_from_staging_buffer(image_info, &staging);

	Vulkan::ImageViewCreateInfo view_info;
	view_info.image = decoded_image.get();
	view_info.view_type = VK_IMAGE_VIEW_TYPE_2D;
	view_info.levels = 1;
	view_info.layers = 1;
	view_info.format = to_storage_format(compressed_format_to_decoded_format(layout.get_format()));

	Vulkan::ImageViewCreateInfo input_view_info;
	input_view_info.image = uploaded_image.get();
	input_view_info.view_type = VK_IMAGE_VIEW_TYPE_2D;
	input_view_info.levels = 1;
	input_view_info.layers = 1;

	cmd.image_barrier(*decoded_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
	                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
	                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT);

	if (!set_compute_decoder(cmd, layout.get_format()))
	{
		LOGE("Failed to set the compute decoder.\n");
		return {};
	}

	for (unsigned level = 0; level < layout.get_levels(); level++)
	{
		uint32_t mip_width = layout.get_width(level);
		uint32_t mip_height = layout.get_height(level);

		for (unsigned layer = 0; layer < layout.get_layers(); layer++)
		{
			view_info.base_layer = input_view_info.base_layer = layer;
			view_info.base_level = input_view_info.base_level = level;
			auto storage_view = device.create_image_view(view_info);
			auto payload_view = device.create_image_view(input_view_info);

			cmd.set_storage_texture(0, 0, *storage_view);
			cmd.set_texture(0, 1, *payload_view);
			dispatch_kernel(cmd, mip_width, mip_height, layout.get_format());
		}
	}

	cmd.image_barrier(*decoded_image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
	                  VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_SHADER_READ_BIT);

	cmd.set_specialization_constant_mask(0);
	return decoded_image;
}
}