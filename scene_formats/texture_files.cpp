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

#include "texture_files.hpp"
#include "gli/load.hpp"
#include "gli/generate_mipmaps.hpp"
#include "stb_image.h"
#include "filesystem.hpp"
#include "gli/save_ktx.hpp"

namespace Granite
{
VkFormat gli_format_to_vulkan(gli::format format)
{
#define fmt(g, vk) \
    case gli::format::FORMAT_##g: \
        return VK_FORMAT_##vk

	switch (format)
	{
	fmt(RGB_ETC2_UNORM_BLOCK8, ETC2_R8G8B8_UNORM_BLOCK);
	fmt(RGBA_ETC2_UNORM_BLOCK8, ETC2_R8G8B8A1_UNORM_BLOCK);
	fmt(RGBA_ETC2_UNORM_BLOCK16, ETC2_R8G8B8A8_UNORM_BLOCK);
	fmt(RGB_ETC2_SRGB_BLOCK8, ETC2_R8G8B8_SRGB_BLOCK);
	fmt(RGBA_ETC2_SRGB_BLOCK8, ETC2_R8G8B8A1_SRGB_BLOCK);
	fmt(RGBA_ETC2_SRGB_BLOCK16, ETC2_R8G8B8A8_SRGB_BLOCK);
	fmt(R_EAC_SNORM_BLOCK8, EAC_R11_SNORM_BLOCK);
	fmt(R_EAC_UNORM_BLOCK8, EAC_R11_UNORM_BLOCK);
	fmt(RG_EAC_SNORM_BLOCK16, EAC_R11G11_SNORM_BLOCK);
	fmt(RG_EAC_UNORM_BLOCK16, EAC_R11G11_UNORM_BLOCK);

	fmt(RGB_DXT1_UNORM_BLOCK8, BC1_RGB_UNORM_BLOCK);
	fmt(RGB_DXT1_SRGB_BLOCK8, BC1_RGB_SRGB_BLOCK);
	fmt(RGBA_DXT1_UNORM_BLOCK8, BC1_RGBA_UNORM_BLOCK);
	fmt(RGBA_DXT1_SRGB_BLOCK8, BC1_RGBA_SRGB_BLOCK);
	fmt(RGBA_DXT3_UNORM_BLOCK16, BC2_UNORM_BLOCK);
	fmt(RGBA_DXT3_SRGB_BLOCK16, BC2_SRGB_BLOCK);
	fmt(RGBA_DXT5_UNORM_BLOCK16, BC3_UNORM_BLOCK);
	fmt(RGBA_DXT5_SRGB_BLOCK16, BC3_SRGB_BLOCK);
	fmt(RGB_BP_UFLOAT_BLOCK16, BC6H_UFLOAT_BLOCK);
	fmt(RGB_BP_SFLOAT_BLOCK16, BC6H_SFLOAT_BLOCK);
	fmt(RGBA_BP_SRGB_BLOCK16, BC7_SRGB_BLOCK);
	fmt(RGBA_BP_UNORM_BLOCK16, BC7_UNORM_BLOCK);

		// ASTC
	fmt(RGBA_ASTC_4X4_SRGB_BLOCK16, ASTC_4x4_SRGB_BLOCK);
	fmt(RGBA_ASTC_5X4_SRGB_BLOCK16, ASTC_5x4_SRGB_BLOCK);
	fmt(RGBA_ASTC_5X5_SRGB_BLOCK16, ASTC_5x5_SRGB_BLOCK);
	fmt(RGBA_ASTC_6X5_SRGB_BLOCK16, ASTC_6x5_SRGB_BLOCK);
	fmt(RGBA_ASTC_6X6_SRGB_BLOCK16, ASTC_6x6_SRGB_BLOCK);
	fmt(RGBA_ASTC_8X5_SRGB_BLOCK16, ASTC_8x5_SRGB_BLOCK);
	fmt(RGBA_ASTC_8X6_SRGB_BLOCK16, ASTC_8x6_SRGB_BLOCK);
	fmt(RGBA_ASTC_8X8_SRGB_BLOCK16, ASTC_8x8_SRGB_BLOCK);
	fmt(RGBA_ASTC_10X5_SRGB_BLOCK16, ASTC_10x5_SRGB_BLOCK);
	fmt(RGBA_ASTC_10X6_SRGB_BLOCK16, ASTC_10x6_SRGB_BLOCK);
	fmt(RGBA_ASTC_10X8_SRGB_BLOCK16, ASTC_10x8_SRGB_BLOCK);
	fmt(RGBA_ASTC_10X10_SRGB_BLOCK16, ASTC_10x10_SRGB_BLOCK);
	fmt(RGBA_ASTC_12X10_SRGB_BLOCK16, ASTC_12x10_SRGB_BLOCK);
	fmt(RGBA_ASTC_12X12_SRGB_BLOCK16, ASTC_12x12_SRGB_BLOCK);
	fmt(RGBA_ASTC_4X4_UNORM_BLOCK16, ASTC_4x4_UNORM_BLOCK);
	fmt(RGBA_ASTC_5X4_UNORM_BLOCK16, ASTC_5x4_UNORM_BLOCK);
	fmt(RGBA_ASTC_5X5_UNORM_BLOCK16, ASTC_5x5_UNORM_BLOCK);
	fmt(RGBA_ASTC_6X5_UNORM_BLOCK16, ASTC_6x5_UNORM_BLOCK);
	fmt(RGBA_ASTC_6X6_UNORM_BLOCK16, ASTC_6x6_UNORM_BLOCK);
	fmt(RGBA_ASTC_8X5_UNORM_BLOCK16, ASTC_8x5_UNORM_BLOCK);
	fmt(RGBA_ASTC_8X6_UNORM_BLOCK16, ASTC_8x6_UNORM_BLOCK);
	fmt(RGBA_ASTC_8X8_UNORM_BLOCK16, ASTC_8x8_UNORM_BLOCK);
	fmt(RGBA_ASTC_10X5_UNORM_BLOCK16, ASTC_10x5_UNORM_BLOCK);
	fmt(RGBA_ASTC_10X6_UNORM_BLOCK16, ASTC_10x6_UNORM_BLOCK);
	fmt(RGBA_ASTC_10X8_UNORM_BLOCK16, ASTC_10x8_UNORM_BLOCK);
	fmt(RGBA_ASTC_10X10_UNORM_BLOCK16, ASTC_10x10_UNORM_BLOCK);
	fmt(RGBA_ASTC_12X10_UNORM_BLOCK16, ASTC_12x10_UNORM_BLOCK);
	fmt(RGBA_ASTC_12X12_UNORM_BLOCK16, ASTC_12x12_UNORM_BLOCK);

	fmt(RGBA8_UNORM_PACK8, R8G8B8A8_UNORM);
	fmt(RGBA8_SRGB_PACK8, R8G8B8A8_SRGB);
	fmt(RGBA32_SFLOAT_PACK32, R32G32B32A32_SFLOAT);
	fmt(RG32_SFLOAT_PACK32, R32G32_SFLOAT);
	fmt(R32_SFLOAT_PACK32, R32_SFLOAT);
	fmt(RGBA16_SFLOAT_PACK16, R16G16B16A16_SFLOAT);
	fmt(RG16_SFLOAT_PACK16, R16G16_SFLOAT);
	fmt(R16_SFLOAT_PACK16, R16_SFLOAT);
	fmt(RGB10A2_UNORM_PACK32, A2B10G10R10_UNORM_PACK32);
	fmt(R8_UNORM_PACK8, R8_UNORM);
	fmt(RG8_UNORM_PACK8, R8G8_UNORM);

	fmt(RG11B10_UFLOAT_PACK32, B10G11R11_UFLOAT_PACK32);

	default:
		return VK_FORMAT_UNDEFINED;
	}

#undef fmt
}

static gli::texture load_stb(const void *data, size_t size, ColorSpace color)
{
	int width, height;
	int components;
	auto *buffer = stbi_load_from_memory(static_cast<const stbi_uc *>(data), size, &width, &height, &components, 4);

	if (!buffer)
		return {};

	gli::texture tex(gli::TARGET_2D,
	                 color == ColorSpace::sRGB ? gli::FORMAT_RGBA8_SRGB_PACK8 : gli::FORMAT_RGBA8_UNORM_PACK8,
	                 {width, height, 1},
	                 1, 1, 1);

	memcpy(tex.data(), buffer, width * height * 4);
	stbi_image_free(buffer);
	return tex;
}

static gli::texture load_hdr(const void *data, size_t size)
{
	int width, height;
	int components;
	auto *buffer = stbi_loadf_from_memory(static_cast<const stbi_uc *>(data), size, &width, &height, &components, 3);

	gli::texture tex(gli::TARGET_2D,
	                 gli::FORMAT_RGBA16_SFLOAT_PACK16,
	                 {width, height, 1},
	                 1, 1, 1);

	auto *converted = static_cast<glm::uvec2 *>(tex.data());
	for (int i = 0; i < width * height; i++)
	{
		converted[i] = glm::uvec2(glm::packHalf2x16(glm::vec2(buffer[3 * i + 0], buffer[3 * i + 1])),
		                          glm::packHalf2x16(glm::vec2(buffer[3 * i + 2], 1.0f)));
	}
	stbi_image_free(buffer);
	return tex;
}

gli::texture load_texture_from_memory(const void *data, size_t size, ColorSpace color)
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
	else
		return gli::load(static_cast<const char *>(data), size);
}

gli::texture load_texture_from_file(const std::string &path, ColorSpace color)
{
	auto file = Filesystem::get().open(path, FileMode::ReadOnly);
	if (!file)
		return {};
	void *mapped = file->map();
	if (!mapped)
		return {};
	auto ret = load_texture_from_memory(mapped, file->get_size(), color);
	file->unmap();
	return ret;
}

bool save_texture_to_file(const std::string &path, const gli::texture &tex)
{
	std::vector<char> memory;
	if (!gli::save_ktx(tex, memory))
		return false;

	auto file = Filesystem::get().open(path, FileMode::WriteOnly);
	if (!file)
		return false;
	void *mapped = file->map_write(memory.size());
	if (!mapped)
		return false;

	memcpy(mapped, memory.data(), memory.size());
	file->unmap();
	return true;
}

static unsigned num_miplevels(int width, int height, int depth)
{
	unsigned size = unsigned(glm::max(glm::max(width, height), depth));
	unsigned levels = 0;
	while (size)
	{
		levels++;
		size >>= 1;
	}
	return levels;
}

gli::texture generate_offline_mipmaps(const gli::texture &tex)
{
	unsigned levels = num_miplevels(tex.extent().x, tex.extent().y, tex.extent().z);
	switch (tex.target())
	{
	case gli::TARGET_2D:
	{
		gli::texture2d input(tex.format(), tex.extent().xy, levels);
		memcpy(input.data(0, 0, 0), tex.data(0, 0, 0), input.size(0));
		return gli::generate_mipmaps(input, gli::FILTER_LINEAR);
	}

	case gli::TARGET_2D_ARRAY:
	{
		gli::texture2d_array input(tex.format(), tex.extent().xy, tex.layers(), levels);
		for (unsigned l = 0; l < tex.layers(); l++)
			memcpy(input.data(l, 0, 0), tex.data(l, 0, 0), tex.size(0));
		return gli::generate_mipmaps(input, gli::FILTER_LINEAR);
	}

	case gli::TARGET_CUBE:
	{
		gli::texture_cube input(tex.format(), tex.extent().xy, levels);
		for (unsigned f = 0; f < tex.faces(); f++)
			memcpy(input.data(0, f, 0), tex.data(0, f, 0), tex.size(0));
		return gli::generate_mipmaps(input, gli::FILTER_LINEAR);
	}

	case gli::TARGET_CUBE_ARRAY:
	{
		gli::texture_cube input(tex.format(), tex.extent().xy, levels);
		for (unsigned l = 0; l < tex.layers(); l++)
			for (unsigned f = 0; f < tex.faces(); f++)
				memcpy(input.data(l, f, 0), tex.data(l, f, 0), tex.size(0));
		return gli::generate_mipmaps(input, gli::FILTER_LINEAR);
	}

	case gli::TARGET_3D:
	{
		gli::texture3d input(tex.format(), tex.extent(), levels);
		memcpy(input.data(0, 0, 0), tex.data(0, 0, 0), tex.size(0));
		return gli::generate_mipmaps(input, gli::FILTER_LINEAR);
	}

	default:
		return {};
	}
}
}