/* Copyright (c) 2017-2018 Hans-Kristian Arntzen
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

#include "vulkan.hpp"
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

static inline VkDeviceSize format_get_layer_size(VkFormat format, unsigned width, unsigned height, unsigned depth)
{
	uint32_t blocks_x = width;
	uint32_t blocks_y = height;
	format_num_blocks(format, blocks_x, blocks_y);
	format_align_dim(format, width, height);

	VkDeviceSize size = TextureFormatLayout::format_block_size(format) * depth * blocks_x * blocks_y;
	return size;
}
}
