/* Copyright (c) 2017 Hans-Kristian Arntzen
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

#include "volk.h"
#include <vector>
#include <stddef.h>
#include <assert.h>

namespace Vulkan
{
class TextureFormatLayout
{
public:
	void set_1d(VkFormat format, uint32_t width, uint32_t array_layers = 1, uint32_t mip_levels = 1);
	void set_2d(VkFormat format, uint32_t width, uint32_t height, uint32_t array_layers = 1, uint32_t mip_levels = 1);
	void set_3d(VkFormat format, uint32_t width, uint32_t height, uint32_t depth, uint32_t mip_levels = 1);

	static uint32_t format_block_size(VkFormat format);
	static void format_block_dim(VkFormat format, uint32_t &width, uint32_t &height);
	static uint32_t num_miplevels(uint32_t width, uint32_t height = 1, uint32_t depth = 1);

	void set_buffer(void *buffer, size_t size);

	uint32_t get_width(uint32_t mip = 0) const;
	uint32_t get_height(uint32_t mip = 0) const;
	uint32_t get_depth(uint32_t mip = 0) const;
	uint32_t get_levels() const;
	uint32_t get_layers() const;

	size_t get_required_size() const;

	struct MipInfo
	{
		size_t offset = 0;
		uint32_t width = 1;
		uint32_t height = 1;
		uint32_t depth = 1;

		uint32_t block_width = 0;
		uint32_t block_height = 0;
		size_t block_image_height = 0;
		size_t block_row_width = 0;
	};

	const MipInfo &get_mipinfo(uint32_t mip) const;

	template <typename T>
	inline T *data_1d(uint32_t x, uint32_t layer = 0, uint32_t mip = 0)
	{
		assert(sizeof(T) == block_stride);
		assert(buffer);
		assert(image_type == VK_IMAGE_TYPE_1D);
		assert(buffer_size == required_size);

		auto &mip_info = mips[mip];
		T *slice = reinterpret_cast<T *>(buffer + mip_info.offset);
		slice += layer * mip_info.block_row_width * mip_info.block_image_height;
		slice += x;
		return slice;
	}

	template <typename T>
	inline T *data_2d(uint32_t x, uint32_t y, uint32_t layer = 0, uint32_t mip = 0)
	{
		assert(sizeof(T) == block_stride);
		assert(buffer);
		assert(image_type == VK_IMAGE_TYPE_2D);
		assert(buffer_size == required_size);

		auto &mip_info = mips[mip];
		T *slice = reinterpret_cast<T *>(buffer + mip_info.offset);
		slice += layer * mip_info.block_row_width * mip_info.block_image_height;
		slice += y * mip_info.block_row_width;
		slice += x;
		return slice;
	}

	template <typename T>
	inline T *data_3d(uint32_t x, uint32_t y, uint32_t z, uint32_t mip = 0)
	{
		assert(sizeof(T) == block_stride);
		assert(buffer);
		assert(image_type == VK_IMAGE_TYPE_3D);
		assert(buffer_size == required_size);

		auto &mip_info = mips[mip];
		T *slice = reinterpret_cast<T *>(buffer + mip_info.offset);
		slice += z * mip_info.block_row_width * mip_info.block_image_height;
		slice += y * mip_info.block_row_width;
		slice += x;
		return slice;
	}

private:
	uint8_t *buffer = nullptr;
	size_t buffer_size = 0;

	VkImageType image_type = VK_IMAGE_TYPE_RANGE_SIZE;
	VkFormat format = VK_FORMAT_UNDEFINED;
	size_t required_size = 0;

	uint32_t block_stride = 1;
	uint32_t mip_levels = 1;
	uint32_t array_layers = 1;
	uint32_t block_dim_x = 1;
	uint32_t block_dim_y = 1;

	MipInfo mips[16];

	void fill_mipinfo(uint32_t width, uint32_t height, uint32_t depth);
};
}