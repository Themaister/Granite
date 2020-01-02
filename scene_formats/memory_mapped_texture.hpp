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

#include "texture_format.hpp"
#include "filesystem.hpp"

namespace Granite
{
namespace SceneFormats
{
enum MemoryMappedTextureFlagBits
{
	MEMORY_MAPPED_TEXTURE_CUBE_MAP_COMPATIBLE_BIT = 1 << 0,
	MEMORY_MAPPED_TEXTURE_GENERATE_MIPMAP_ON_LOAD_BIT = 1 << 1,
	MEMORY_MAPPED_TEXTURE_SWIZZLE_R_SHIFT = 16,
	MEMORY_MAPPED_TEXTURE_SWIZZLE_G_SHIFT = 19,
	MEMORY_MAPPED_TEXTURE_SWIZZLE_B_SHIFT = 22,
	MEMORY_MAPPED_TEXTURE_SWIZZLE_A_SHIFT = 25,
	MEMORY_MAPPED_TEXTURE_SWIZZLE_MASK = 0x7
};
using MemoryMappedTextureFlags = uint32_t;

class MemoryMappedTexture
{
public:
	void set_1d(VkFormat format, uint32_t width, uint32_t layers = 1, uint32_t levels = 1);
	void set_2d(VkFormat format, uint32_t width, uint32_t height, uint32_t layers = 1, uint32_t levels = 1);
	void set_3d(VkFormat format, uint32_t width, uint32_t height, uint32_t depth, uint32_t levels = 1);
	void set_cube(VkFormat format, uint32_t size, uint32_t cube_layers = 1, uint32_t levels = 1);

	static bool is_header(const void *mapped, size_t size);

	bool map_write(const std::string &path);
	bool map_write(std::unique_ptr<Granite::File> file, void *mapped);
	bool map_read(const std::string &path);
	bool map_read(std::unique_ptr<Granite::File> file, void *mapped);
	bool map_copy(const void *mapped, size_t size);
	bool map_write_scratch();
	bool copy_to_path(const std::string &path);
	void make_local_copy();

	inline const Vulkan::TextureFormatLayout &get_layout() const
	{
		return layout;
	}

	void set_generate_mipmaps_on_load(bool enable = true);

	MemoryMappedTextureFlags get_flags() const;
	void set_flags(MemoryMappedTextureFlags flags);

	size_t get_required_size() const;
	void set_swizzle(const VkComponentMapping &swizzle);

	void remap_swizzle(VkComponentMapping &mapping) const;

	inline bool empty() const
	{
		return get_layout().get_required_size() == 0;
	}

private:
	Vulkan::TextureFormatLayout layout;
	std::unique_ptr<Granite::File> file;
	uint8_t *mapped = nullptr;
	bool cube = false;
	bool mipgen_on_load = false;
	VkComponentMapping swizzle = {
		VK_COMPONENT_SWIZZLE_R,
		VK_COMPONENT_SWIZZLE_G,
		VK_COMPONENT_SWIZZLE_B,
		VK_COMPONENT_SWIZZLE_A,
	};
};
}
}
