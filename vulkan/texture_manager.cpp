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
#include "texture_loading.hpp"

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
	update_gli(data, size);
	device->get_texture_manager().notify_updated_texture(path, *this);
}

void Texture::update_gli(const void *data, size_t size)
{
	gli::texture tex = Granite::load_texture_from_memory(data, size,
	                                                     (format == VK_FORMAT_R8G8B8A8_SRGB ||
	                                                      format == VK_FORMAT_B8G8R8A8_SRGB ||
	                                                      format == VK_FORMAT_A8B8G8R8_SRGB_PACK32) ?
	                                                     Granite::ColorSpace::sRGB : Granite::ColorSpace::Linear);
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
	info.format = Granite::gli_format_to_vulkan(tex.format());

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
