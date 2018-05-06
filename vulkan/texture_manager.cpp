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

#include "texture_manager.hpp"
#include "device.hpp"
#include "stb_image.h"
#include "texture_files.hpp"
#include "thread_group.hpp"
#include "memory_mapped_texture.hpp"

using namespace std;
using namespace Granite;
using namespace Granite::SceneFormats;

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
			if (MemoryMappedTexture::is_header(mapped, size))
				update_gtx(move(file), mapped);
			else
				update_other(mapped, size);
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

void Texture::update_gtx(const MemoryMappedTexture &mapped_file)
{
	auto &layout = mapped_file.get_layout();

	ImageCreateInfo info = {};
	info.width = layout.get_width();
	info.height = layout.get_height();
	info.depth = layout.get_depth();
	info.type = layout.get_image_type();
	info.format = layout.get_format();
	info.levels = layout.get_levels();
	info.layers = layout.get_layers();
	info.samples = VK_SAMPLE_COUNT_1_BIT;
	info.domain = ImageDomain::Physical;
	info.initial_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
	info.swizzle = swizzle;
	info.flags = (mapped_file.get_flags() & SceneFormats::MEMORY_MAPPED_TEXTURE_CUBE_MAP_COMPATIBLE_BIT) ?
	             VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
	info.misc = 0;

	if (info.levels == 1 &&
	    (mapped_file.get_flags() & SceneFormats::MEMORY_MAPPED_TEXTURE_GENERATE_MIPMAP_ON_LOAD_BIT) != 0 &&
	    device->image_format_is_supported(info.format, VK_FORMAT_FEATURE_BLIT_SRC_BIT) &&
	    device->image_format_is_supported(info.format, VK_FORMAT_FEATURE_BLIT_DST_BIT))
	{
		info.levels = 0;
		info.misc |= IMAGE_MISC_GENERATE_MIPS_BIT;
	}

	if (!device->image_format_is_supported(info.format, VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
	{
		LOGE("Format (%u) is not supported!\n", unsigned(info.format));
		return;
	}

	auto staging = device->create_image_staging_buffer(layout);
	auto image = device->create_image_from_staging_buffer(info, &staging);
	if (image)
		device->set_name(*image, path.c_str());
	replace_image(image);
}

void Texture::update_gtx(unique_ptr<File> file, void *mapped)
{
	SceneFormats::MemoryMappedTexture mapped_file;
	if (!mapped_file.map_read(move(file), mapped))
	{
		LOGE("Failed to read texture.\n");
		return;
	}

	update_gtx(mapped_file);
}

void Texture::update_other(const void *data, size_t size)
{
	auto tex = load_texture_from_memory(data, size,
	                                    (format == VK_FORMAT_R8G8B8A8_SRGB ||
	                                     format == VK_FORMAT_UNDEFINED ||
	                                     format == VK_FORMAT_B8G8R8A8_SRGB ||
	                                     format == VK_FORMAT_A8B8G8R8_SRGB_PACK32) ?
	                                    ColorSpace::sRGB : ColorSpace::Linear);

	update_gtx(tex);
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
