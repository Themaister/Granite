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

#include "logging.hpp"
#include "memory_mapped_texture.hpp"
#include <string.h>
#include <vector>
#include <utility>

using namespace Granite;
using namespace Granite::SceneFormats;

int main(int argc, char *argv[])
{
	if (argc < 4)
	{
		LOGE("Usage: %s <output> <cube|2D> <inputs>...\n", argv[0]);
		return 1;
	}

	Global::init();

	std::vector<MemoryMappedTexture> inputs;
	VkFormat fmt = VK_FORMAT_UNDEFINED;
	unsigned width = 0;
	unsigned height = 0;
	unsigned levels = 0;
	unsigned total_layers = 0;
	MemoryMappedTextureFlags flags = 0;
	bool generate_mips = false;

	bool cube = strcmp(argv[2], "cube") == 0;
	bool type_2d = strcmp(argv[2], "2D") == 0;
	if (!type_2d && !cube)
	{
		LOGE("Usage: %s <output> <cube|2D> <inputs>...\n", argv[0]);
		return 1;
	}

	for (int i = 3; i < argc; i++)
	{
		MemoryMappedTexture tex;
		if (!tex.map_read(argv[i]) || tex.empty())
		{
			LOGE("Failed to load texture: %s\n", argv[i]);
			return 1;
		}

		if (fmt != VK_FORMAT_UNDEFINED)
		{
			if (tex.get_layout().get_format() != fmt)
			{
				LOGE("Format mismatch!\n");
				return 1;
			}

			if (tex.get_layout().get_width() != width)
			{
				LOGE("Mismatch width\n");
				return 1;
			}

			if (tex.get_layout().get_height() != height)
			{
				LOGE("Mismatch height\n");
				return 1;
			}

			if (tex.get_layout().get_levels() != levels)
			{
				LOGE("Mismatch levels\n");
				return 1;
			}

			if (tex.get_flags() != flags)
			{
				LOGE("Mismatch flags\n");
				return 1;
			}
		}

		if (tex.get_layout().get_image_type() != VK_IMAGE_TYPE_2D)
		{
			LOGE("Input can only be 2D textures\n");
			return 1;
		}

		fmt = tex.get_layout().get_format();
		width = tex.get_layout().get_width();
		height = tex.get_layout().get_height();
		levels = tex.get_layout().get_levels();
		flags = tex.get_flags();

		if (tex.get_flags() & MEMORY_MAPPED_TEXTURE_GENERATE_MIPMAP_ON_LOAD_BIT)
			generate_mips = true;

		total_layers += tex.get_layout().get_layers();
		inputs.push_back(std::move(tex));
	}

	MemoryMappedTexture array;
	if (cube)
	{
		if ((total_layers % 6) != 0)
		{
			LOGE("Total layers for a cube must be divisible by 6.\n");
			return 1;
		}
		array.set_cube(fmt, width, total_layers / 6, levels);
		array.set_flags(flags | MEMORY_MAPPED_TEXTURE_CUBE_MAP_COMPATIBLE_BIT);
	}
	else
	{
		array.set_2d(fmt, width, height, total_layers, levels);
		array.set_flags(flags & ~MEMORY_MAPPED_TEXTURE_CUBE_MAP_COMPATIBLE_BIT);
	}

	if (generate_mips)
		array.set_generate_mipmaps_on_load(true);

	if (!array.map_write(argv[1]))
	{
		LOGE("Failed to save file: %s\n", argv[1]);
		return 1;
	}

	unsigned output_layer = 0;
	for (auto &input : inputs)
	{
		auto &layout = input.get_layout();
		for (unsigned layer = 0; layer < layout.get_layers(); layer++)
		{
			for (unsigned level = 0; level < levels; level++)
			{
				auto *dst = array.get_layout().data(output_layer, level);
				auto *src = layout.data(layer, level);
				size_t size = array.get_layout().get_layer_size(level);
				memcpy(dst, src, size);
			}

			output_layer++;
		}
	}

	return 0;
}