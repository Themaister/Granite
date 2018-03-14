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
#include "texture_files.hpp"
#include "thread_group.hpp"

using namespace std;

namespace Vulkan
{
Texture::Texture(Device *device, const std::string &path, VkFormat format, const VkComponentMapping &swizzle)
	: VolatileSource(path), device(device), format(format), swizzle(swizzle)
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

void Texture::update(std::unique_ptr<Granite::File> file)
{
	auto &workers = Granite::ThreadGroup::get_global();

	// Workaround, cannot copy the lambda because of owning a unique_ptr.
	auto *f = file.release();
	auto task = workers.create_task([f, this]() {
		LOGI("Loading texture in thread index: %u\n", Granite::ThreadGroup::get_current_thread_index());
		unique_ptr<Granite::File> file{f};
		auto size = file->get_size();
		void *mapped = file->map();
		if (size && mapped)
		{
			update_gli(mapped, size);
			device->get_texture_manager().notify_updated_texture(path, *this);
		}
		else
		{
			LOGE("Failed to map texture file ...\n");
			auto old = handle.write_object({});
			if (old)
				device->keep_handle_alive(move(old));
		}
	});
	task->flush();
}

void Texture::update_gli(const void *data, size_t size)
{
	gli::texture tex = Granite::load_texture_from_memory(data, size,
	                                                     (format == VK_FORMAT_R8G8B8A8_SRGB ||
	                                                      format == VK_FORMAT_UNDEFINED ||
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
	info.swizzle = swizzle;

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

	// Auto-generate mips for single-mip level images.
	if (info.levels == 1 &&
	    device->format_is_supported(info.format, VK_FORMAT_FEATURE_BLIT_SRC_BIT) &&
	    device->format_is_supported(info.format, VK_FORMAT_FEATURE_BLIT_DST_BIT))
	{
		info.levels = 0;
		info.misc |= IMAGE_MISC_GENERATE_MIPS_BIT;
	}

	replace_image(device->create_image(info, initial.data()));
}

void Texture::load()
{
	if (!handle.get_nowait())
		init();
}

void Texture::unload()
{
	deinit();
	handle.reset();
}

void Texture::replace_image(ImageHandle handle)
{
	auto old = this->handle.write_object(move(handle));
	if (old)
		device->keep_handle_alive(move(old));
	device->get_texture_manager().notify_updated_texture(path, *this);
}

Image *Texture::get_image()
{
	auto ret = handle.get();
	VK_ASSERT(ret);
	return ret;
}

TextureManager::TextureManager(Device *device)
	: device(device)
{
}

Texture *TextureManager::request_texture(const std::string &path, VkFormat format, const VkComponentMapping &mapping)
{
	Util::Hasher hasher;
	hasher.string(path);
	auto deferred_hash = hasher.get();
	hasher.u32(format);
	hasher.u32(mapping.r);
	hasher.u32(mapping.g);
	hasher.u32(mapping.b);
	hasher.u32(mapping.a);
	auto hash = hasher.get();

	auto *ret = deferred_textures.find(deferred_hash);
	if (ret)
		return ret;

	ret = textures.find(hash);
	if (ret)
		return ret;

	auto texture = make_unique<Texture>(device, path, format, mapping);
	ret = textures.insert(hash, move(texture));
	return ret;
}

void TextureManager::register_texture_update_notification(const std::string &modified_path,
                                                          std::function<void(Texture &)> func)
{
	Util::Hasher hasher;
	hasher.string(modified_path);
	auto hash = hasher.get();
	auto *ret = deferred_textures.find(hash);
	if (ret)
		func(*ret);

	//lock_guard<mutex> holder{notification_lock};
	notifications[modified_path].push_back(move(func));
}

void TextureManager::notify_updated_texture(const std::string &path, Vulkan::Texture &texture)
{
	//lock_guard<mutex> holder{notification_lock};
	for (auto &n : notifications[path])
		if (n)
			n(texture);
}

Texture *TextureManager::register_deferred_texture(const std::string &path)
{
	Util::Hasher hasher;
	hasher.string(path);
	auto hash = hasher.get();

	auto *ret = deferred_textures.find(hash);
	if (!ret)
	{
		auto texture = make_unique<Texture>(device);
		texture->set_path(path);
		ret = deferred_textures.insert(hash, move(texture));
	}
	return ret;
}

}
