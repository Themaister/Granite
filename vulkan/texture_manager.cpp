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

#include "texture_manager.hpp"
#include "device.hpp"
#include "stb_image.h"

#include "gli/load.hpp"

using namespace std;

namespace Vulkan
{
Texture::Texture(Device *device, const std::string &path, VkFormat format)
	: VolatileSource(path), device(device), format(format)
{
	init();
}

Texture::Texture(Device *device)
	: device(device), format(VK_FORMAT_UNDEFINED)
{
}

void Texture::set_path(const std::string &path)
{
	this->path = path;
}

void Texture::update(const void *data, size_t size)
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
		update_stb(data, size);
	else if (size >= 2 && memcmp(data, jpg_magic, sizeof(jpg_magic)) == 0)
		update_stb(data, size);
	else if (size >= sizeof(hdr_magic) && memcmp(data, hdr_magic, sizeof(hdr_magic)) == 0)
		update_hdr(data, size);
	else
		update_gli(data, size);

	device->get_texture_manager().notify_updated_texture(path, *this);
}

static VkFormat gli_format_to_vk(gli::format format)
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

	default:
		return VK_FORMAT_UNDEFINED;
	}

#undef fmt
}

void Texture::update_gli(const void *data, size_t size)
{
	gli::texture tex = gli::load(static_cast<const char *>(data), size);
	if (tex.empty())
	{
		LOGE("Texture is empty.");
		return;
	}

	ImageCreateInfo info = {};
	info.domain = ImageDomain::Physical;
	info.layers = tex.layers();
	info.levels = tex.levels();
	info.width = tex.extent(0).x;
	info.height = tex.extent(0).y;
	info.depth = tex.extent(0).z;
	info.samples = VK_SAMPLE_COUNT_1_BIT;
	info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
	info.initial_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	info.format = gli_format_to_vk(tex.format());

	if (!device->format_is_supported(info.format, VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
	{
		LOGE("Format is not supported!\n");
		return;
	}

	unsigned faces = 1;

	switch (tex.target())
	{
	case gli::target::TARGET_1D_ARRAY:
		info.misc |= IMAGE_MISC_FORCE_ARRAY_BIT;
		info.type = VK_IMAGE_TYPE_1D;
		info.height = 1;
		info.depth = 1;
		break;

	case gli::target::TARGET_1D:
		info.type = VK_IMAGE_TYPE_1D;
		info.height = 1;
		info.depth = 1;
		break;

	case gli::target::TARGET_2D_ARRAY:
		info.misc |= IMAGE_MISC_FORCE_ARRAY_BIT;
		info.depth = 1;
		info.type = VK_IMAGE_TYPE_2D;
		break;

	case gli::target::TARGET_2D:
		info.depth = 1;
		info.type = VK_IMAGE_TYPE_2D;
		break;

	case gli::target::TARGET_CUBE_ARRAY:
		info.misc |= IMAGE_MISC_FORCE_ARRAY_BIT;
		info.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
		info.depth = 1;
		faces = tex.faces();
		info.type = VK_IMAGE_TYPE_2D;
		break;

	case gli::target::TARGET_CUBE:
		info.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
		info.depth = 1;
		faces = tex.faces();
		info.type = VK_IMAGE_TYPE_2D;
		break;

	case gli::target::TARGET_3D:
		info.type = VK_IMAGE_TYPE_3D;
		break;

	default:
		LOGE("Unknown target type.\n");
		return;
	}

	vector<ImageInitialData> initial;
	initial.reserve(info.levels * faces * info.layers);

	for (unsigned level = 0; level < info.levels; level++)
	{
		for (unsigned layer = 0; layer < info.layers; layer++)
		{
			for (unsigned face = 0; face < faces; face++)
			{
				auto *mip = tex.data(layer, face, level);
				initial.push_back({mip, 0, 0});
			}
		}
	}

	info.layers *= faces;
	handle = device->create_image(info, initial.data());
}

void Texture::update_hdr(const void *data, size_t size)
{
	int width, height;
	int components;
	auto *buffer = stbi_loadf_from_memory(static_cast<const stbi_uc *>(data), size, &width, &height, &components, 3);

	// RGB9E5 might be a better choice here, but needs complex conversion.
	auto desc = ImageCreateInfo::immutable_2d_image(unsigned(width), unsigned(height),
	                                                VK_FORMAT_R16G16B16A16_SFLOAT, true);

	ImageInitialData initial = {};
	vector<glm::uvec2> converted(width * height);
	for (int i = 0; i < width * height; i++)
	{
		converted[i] = glm::uvec2(glm::packHalf2x16(glm::vec2(buffer[3 * i + 0], buffer[3 * i + 1])),
		                          glm::packHalf2x16(glm::vec2(buffer[3 * i + 2], 1.0f)));
	}
	initial.data = converted.data();
	handle = device->create_image(desc, &initial);
	stbi_image_free(buffer);
}

void Texture::update_stb(const void *data, size_t size)
{
	int width, height;
	int components;
	auto *buffer = stbi_load_from_memory(static_cast<const stbi_uc *>(data), size, &width, &height, &components, 4);

	if (!buffer)
		throw runtime_error("stbi_load_from_memory failed.");

	handle.reset();
	auto desc = ImageCreateInfo::immutable_2d_image(unsigned(width), unsigned(height),
	                                                format != VK_FORMAT_UNDEFINED ? format : VK_FORMAT_R8G8B8A8_SRGB,
	                                                true);

	ImageInitialData initial = {};
	initial.data = buffer;
	handle = device->create_image(desc, &initial);
	stbi_image_free(buffer);
}

void Texture::load()
{
	if (!handle)
		init();
}

void Texture::unload()
{
	deinit();
	handle.reset();
}

void Texture::replace_image(ImageHandle handle)
{
	this->handle = handle;
	device->get_texture_manager().notify_updated_texture(path, *this);
}

TextureManager::TextureManager(Device *device)
	: device(device)
{
}

Texture *TextureManager::request_texture(const std::string &path, VkFormat format)
{
	auto itr = textures.find(path);
	if (itr == end(textures))
	{
		unique_ptr<Texture> texture(new Texture(device, path, format));
		auto *ret = texture.get();
		textures[path] = move(texture);
		return ret;
	}
	else
		return itr->second.get();
}

void TextureManager::register_texture_update_notification(const std::string &modified_path,
                                                          std::function<void(Texture &)> func)
{
	auto itr = textures.find(modified_path);
	if (itr != end(textures))
		func(*itr->second);
	notifications[modified_path].push_back(move(func));
}

void TextureManager::notify_updated_texture(const std::string &path, Vulkan::Texture &texture)
{
	for (auto &n : notifications[path])
		if (n)
			n(texture);
}

Texture *TextureManager::register_deferred_texture(const std::string &path)
{
	auto itr = textures.find(path);
	if (itr == end(textures))
	{
		unique_ptr<Texture> texture(new Texture(device));
		auto *ret = texture.get();
		texture->set_path(path);
		textures[path] = move(texture);
		return ret;
	}
	else
		return itr->second.get();
}

}