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

#include "texture_files.hpp"
#include "stb_image.h"
#include "filesystem.hpp"
#include "muglm/muglm_impl.hpp"
#include <string.h>

namespace Granite
{
static SceneFormats::MemoryMappedTexture load_stb(const void *data, size_t size, ColorSpace color)
{
	int width, height;
	int components;
	auto *buffer = stbi_load_from_memory(static_cast<const stbi_uc *>(data), size, &width, &height, &components, 4);

	if (!buffer)
		return {};

	SceneFormats::MemoryMappedTexture tex;
	tex.set_2d(color == ColorSpace::sRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM, width, height);
	tex.set_generate_mipmaps_on_load(true);
	if (!tex.map_write_scratch())
		return {};

	memcpy(tex.get_layout().data(), buffer, width * height * 4);
	stbi_image_free(buffer);
	return tex;
}

static SceneFormats::MemoryMappedTexture load_hdr(const void *data, size_t size)
{
	int width, height;
	int components;
	auto *buffer = stbi_loadf_from_memory(static_cast<const stbi_uc *>(data), size, &width, &height, &components, 3);

	SceneFormats::MemoryMappedTexture tex;
	tex.set_2d(VK_FORMAT_R16G16B16A16_SFLOAT, width, height);
	if (!tex.map_write_scratch())
		return {};
	tex.set_generate_mipmaps_on_load(true);

	auto *converted = static_cast<muglm::u16vec4 *>(tex.get_layout().data());
	for (int i = 0; i < width * height; i++)
	{
		converted[i] = muglm::floatToHalf(muglm::vec4(buffer[3 * i + 0], buffer[3 * i + 1], buffer[3 * i + 2], 1.0f));
	}
	stbi_image_free(buffer);
	return tex;
}

SceneFormats::MemoryMappedTexture load_texture_from_memory(const void *data, size_t size, ColorSpace color)
{
	static const uint8_t png_magic[] = {
		0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a,
	};

	static const uint8_t jpg_magic[] = {
		0xff, 0xd8,
	};

	static const uint8_t hdr_magic[] = {
		0x23, 0x3f, 0x52, 0x41, 0x44, 0x49, 0x41, 0x4e, 0x43, 0x45, 0x0a,
	};

	if (size >= sizeof(png_magic) && memcmp(data, png_magic, sizeof(png_magic)) == 0)
		return load_stb(data, size, color);
	else if (size >= 2 && memcmp(data, jpg_magic, sizeof(jpg_magic)) == 0)
		return load_stb(data, size, color);
	else if (size >= sizeof(hdr_magic) && memcmp(data, hdr_magic, sizeof(hdr_magic)) == 0)
		return load_hdr(data, size);
	else if (SceneFormats::MemoryMappedTexture::is_header(data, size))
	{
		SceneFormats::MemoryMappedTexture mapped;
		mapped.map_copy(data, size);
		return mapped;
	}
	else
	{
		// YOLO!
		return load_stb(data, size, color);
	}
}

SceneFormats::MemoryMappedTexture load_texture_from_file(const std::string &path, ColorSpace color)
{
	auto file = Granite::Global::filesystem()->open(path, FileMode::ReadOnly);
	if (!file)
		return {};

	void *mapped = file->map();
	if (!mapped)
		return {};

	if (SceneFormats::MemoryMappedTexture::is_header(mapped, file->get_size()))
	{
		SceneFormats::MemoryMappedTexture tex;
		tex.map_read(move(file), mapped);
		return tex;
	}

	return load_texture_from_memory(mapped, file->get_size(), color);
}
}