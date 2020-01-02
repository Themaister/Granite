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

#pragma once

#include "vulkan_headers.hpp"
#include "texture_format.hpp"

namespace Vulkan
{
static inline bool format_is_srgb(VkFormat format)
{
	switch (format)
	{
	case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
	case VK_FORMAT_R8G8B8A8_SRGB:
	case VK_FORMAT_B8G8R8A8_SRGB:
	case VK_FORMAT_R8_SRGB:
	case VK_FORMAT_R8G8_SRGB:
	case VK_FORMAT_R8G8B8_SRGB:
	case VK_FORMAT_B8G8R8_SRGB:
		return true;

	default:
		return false;
	}
}

static inline bool format_has_depth_aspect(VkFormat format)
{
	switch (format)
	{
	case VK_FORMAT_D16_UNORM:
	case VK_FORMAT_D16_UNORM_S8_UINT:
	case VK_FORMAT_D24_UNORM_S8_UINT:
	case VK_FORMAT_D32_SFLOAT:
	case VK_FORMAT_X8_D24_UNORM_PACK32:
	case VK_FORMAT_D32_SFLOAT_S8_UINT:
		return true;

	default:
		return false;
	}
}

static inline bool format_has_stencil_aspect(VkFormat format)
{
	switch (format)
	{
	case VK_FORMAT_D16_UNORM_S8_UINT:
	case VK_FORMAT_D24_UNORM_S8_UINT:
	case VK_FORMAT_D32_SFLOAT_S8_UINT:
	case VK_FORMAT_S8_UINT:
		return true;

	default:
		return false;
	}
}

static inline bool format_has_depth_or_stencil_aspect(VkFormat format)
{
	return format_has_depth_aspect(format) || format_has_stencil_aspect(format);
}

static inline VkImageAspectFlags format_to_aspect_mask(VkFormat format)
{
	switch (format)
	{
	case VK_FORMAT_UNDEFINED:
		return 0;

	case VK_FORMAT_S8_UINT:
		return VK_IMAGE_ASPECT_STENCIL_BIT;

	case VK_FORMAT_D16_UNORM_S8_UINT:
	case VK_FORMAT_D24_UNORM_S8_UINT:
	case VK_FORMAT_D32_SFLOAT_S8_UINT:
		return VK_IMAGE_ASPECT_STENCIL_BIT | VK_IMAGE_ASPECT_DEPTH_BIT;

	case VK_FORMAT_D16_UNORM:
	case VK_FORMAT_D32_SFLOAT:
	case VK_FORMAT_X8_D24_UNORM_PACK32:
		return VK_IMAGE_ASPECT_DEPTH_BIT;

	default:
		return VK_IMAGE_ASPECT_COLOR_BIT;
	}
}

static inline void format_align_dim(VkFormat format, uint32_t &width, uint32_t &height)
{
	uint32_t align_width, align_height;
	TextureFormatLayout::format_block_dim(format, align_width, align_height);
	width = ((width + align_width - 1) / align_width) * align_width;
	height = ((height + align_height - 1) / align_height) * align_height;
}

static inline void format_num_blocks(VkFormat format, uint32_t &width, uint32_t &height)
{
	uint32_t align_width, align_height;
	TextureFormatLayout::format_block_dim(format, align_width, align_height);
	width = (width + align_width - 1) / align_width;
	height = (height + align_height - 1) / align_height;
}

static inline VkDeviceSize format_get_layer_size(VkFormat format, VkImageAspectFlags aspect, unsigned width, unsigned height, unsigned depth)
{
	uint32_t blocks_x = width;
	uint32_t blocks_y = height;
	format_num_blocks(format, blocks_x, blocks_y);
	format_align_dim(format, width, height);

	VkDeviceSize size = TextureFormatLayout::format_block_size(format, aspect) * depth * blocks_x * blocks_y;
	return size;
}

enum class YCbCrFormat
{
	YUV420P_3PLANE,
	YUV444P_3PLANE,
	YUV422P_3PLANE,
	Count
};

static inline unsigned format_ycbcr_num_planes(VkFormat format)
{
	switch (format)
	{
	case VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM:
	case VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM:
	case VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM:
	case VK_FORMAT_G16_B16_R16_3PLANE_420_UNORM:
	case VK_FORMAT_G16_B16_R16_3PLANE_422_UNORM:
	case VK_FORMAT_G16_B16_R16_3PLANE_444_UNORM:
	case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16:
	case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16:
	case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16:
	case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16:
	case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16:
	case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16:
		return 3;

	case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
	case VK_FORMAT_G8_B8R8_2PLANE_422_UNORM:
	case VK_FORMAT_G16_B16R16_2PLANE_420_UNORM:
	case VK_FORMAT_G16_B16R16_2PLANE_422_UNORM:
	case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16:
	case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16:
	case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16:
	case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16:
		return 2;

	default:
		return 1;
	}
}

static inline unsigned format_ycbcr_num_planes(YCbCrFormat format)
{
	switch (format)
	{
	case YCbCrFormat::YUV420P_3PLANE:
	case YCbCrFormat::YUV422P_3PLANE:
	case YCbCrFormat::YUV444P_3PLANE:
		return 3;

	default:
		return 0;
	}
}

static inline void format_ycbcr_downsample_dimensions(VkFormat format, VkImageAspectFlags aspect, uint32_t &width, uint32_t &height)
{
	if (aspect == VK_IMAGE_ASPECT_PLANE_0_BIT)
		return;

	switch (format)
	{
#define fmt(x, sub0, sub1) \
	case VK_FORMAT_##x: \
		width >>= sub0; \
		height >>= sub1; \
		break

		fmt(G8_B8_R8_3PLANE_420_UNORM, 1, 1);
		fmt(G8_B8R8_2PLANE_420_UNORM, 1, 1);
		fmt(G8_B8_R8_3PLANE_422_UNORM, 1, 0);
		fmt(G8_B8R8_2PLANE_422_UNORM, 1, 0);
		fmt(G8_B8_R8_3PLANE_444_UNORM, 0, 0);

		fmt(G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16, 1, 1);
		fmt(G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16, 1, 0);
		fmt(G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16, 0, 0);
		fmt(G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16, 1, 1);
		fmt(G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16, 1, 0);

		fmt(G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16, 1, 1);
		fmt(G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16, 1, 0);
		fmt(G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16, 0, 0);
		fmt(G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16, 1, 1);
		fmt(G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16, 1, 0);

		fmt(G16_B16_R16_3PLANE_420_UNORM, 1, 1);
		fmt(G16_B16_R16_3PLANE_422_UNORM, 1, 0);
		fmt(G16_B16_R16_3PLANE_444_UNORM, 0, 0);
		fmt(G16_B16R16_2PLANE_420_UNORM, 1, 1);
		fmt(G16_B16R16_2PLANE_422_UNORM, 1, 0);

	default:
		break;
	}
#undef fmt
}

static inline unsigned format_ycbcr_downsample_ratio_log2(YCbCrFormat format, unsigned dim, unsigned plane)
{
	switch (format)
	{
	case YCbCrFormat::YUV420P_3PLANE:
		return plane > 0 ? 1 : 0;
	case YCbCrFormat::YUV422P_3PLANE:
		return plane > 0 && dim == 0 ? 1 : 0;

	default:
		return 0;
	}
}

static inline VkFormat format_ycbcr_plane_vk_format(YCbCrFormat format, unsigned plane)
{
	switch (format)
	{
	case YCbCrFormat::YUV420P_3PLANE:
		return VK_FORMAT_R8_UNORM;
	case YCbCrFormat::YUV422P_3PLANE:
		return plane > 0 ? VK_FORMAT_R8G8_UNORM : VK_FORMAT_R8_UNORM;
	case YCbCrFormat::YUV444P_3PLANE:
		return VK_FORMAT_R8_UNORM;

	default:
		return VK_FORMAT_UNDEFINED;
	}
}

static inline VkFormat format_ycbcr_planar_vk_format(YCbCrFormat format)
{
	switch (format)
	{
	case YCbCrFormat::YUV420P_3PLANE:
		return VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM;
	case YCbCrFormat::YUV422P_3PLANE:
		return VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM;
	case YCbCrFormat::YUV444P_3PLANE:
		return VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM;

	default:
		return VK_FORMAT_UNDEFINED;
	}
}

}
