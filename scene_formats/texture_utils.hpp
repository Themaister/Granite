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

#include "memory_mapped_texture.hpp"
#include "math.hpp"
#include "muglm/muglm_impl.hpp"
#include <string.h>
#include <stdexcept>

namespace Granite
{
namespace SceneFormats
{
template <typename T, typename Op>
inline void transform_texture_layout(const Vulkan::TextureFormatLayout &layout, const Op &op)
{
	auto levels = layout.get_levels();
	auto layers = layout.get_layers();
	for (uint32_t level = 0; level < levels; level++)
	{
		auto &info = layout.get_mip_info(level);
		uint32_t width = info.block_row_length;
		uint32_t height = info.block_image_height;
		uint32_t depth = info.depth;
		for (uint32_t layer = 0; layer < layers; layer++)
		{
			for (uint32_t z = 0; z < depth; z++)
			{
				for (uint32_t y = 0; y < height; y++)
				{
					for (uint32_t x = 0; x < width; x++)
					{
						// Either z or layer must be 0.
						auto *data = layout.data_generic<T>(x, y, z | layer, level);
						*data = op(*data);
					}
				}
			}
		}
	}
}

MemoryMappedTexture generate_mipmaps(const Vulkan::TextureFormatLayout &layout, MemoryMappedTextureFlags flags);
MemoryMappedTexture generate_mipmaps_to_file(const std::string &path, const Vulkan::TextureFormatLayout &layout, MemoryMappedTextureFlags flags);
MemoryMappedTexture fixup_alpha_edges(const Vulkan::TextureFormatLayout &layout, MemoryMappedTextureFlags flags);

bool swizzle_image(MemoryMappedTexture &texture, const VkComponentMapping &swizzle);

enum class TransparencyType
{
	None,
	Binary,
	Floating
};
TransparencyType image_slice_contains_transparency(const Vulkan::TextureFormatLayout &layout, unsigned layer, unsigned level);
}
}