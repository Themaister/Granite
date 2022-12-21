/* Copyright (c) 2017-2022 Hans-Kristian Arntzen
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
#include "memory_mapped_texture.hpp"
#include "texture_files.hpp"
#include "texture_decoder.hpp"

#include "thread_group.hpp"
#include "thread_id.hpp"

namespace Vulkan
{
bool Texture::init_texture()
{
	if (!path.empty())
		return init();
	else
		return true;
}

Texture::Texture(Device *device_, const std::string &path_, VkFormat format_) :
#ifndef GRANITE_SHIPPING
	VolatileSource(device_->get_system_handles().filesystem, path_),
#else
	path(path_),
#endif
	device(device_), format(format_)
{
}

#ifdef GRANITE_SHIPPING
bool Texture::init()
{
	auto *fs = device->get_system_handles().filesystem;

	if (path.empty() || !fs)
		return false;

	auto file = fs->open_readonly_mapping(path);
	if (!file)
	{
		LOGE("Failed to open volatile file: %s\n", path.c_str());
		return false;
	}

	update(std::move(file));
	return true;
}
#endif

Texture::Texture(Device *device_)
	: device(device_), format(VK_FORMAT_UNDEFINED)
{
}

void Texture::set_path(const std::string &path_)
{
	path = path_;
}

void Texture::update(Granite::FileMappingHandle file)
{
	auto work = [file, this]() mutable {
#if defined(VULKAN_DEBUG)
		LOGI("Loading texture in thread index: %u\n", Util::get_current_thread_index());
#endif
		if (file->get_size())
		{
			if (MemoryMappedTexture::is_header(file->data(), file->get_size()))
				update_gtx(std::move(file));
			else
				update_other(file->data(), file->get_size());
		}
		else
		{
			LOGE("Failed to map texture file ...\n");
			update_checkerboard();
		}
	};

	if (auto *group = device->get_system_handles().thread_group)
	{
		auto task = group->create_task(std::move(work));
		task->set_desc("texture-load");
		task->set_task_class(Granite::TaskClass::Background);
	}
	else
		work();
}

void Texture::update_checkerboard()
{
	LOGE("Failed to load texture: %s, falling back to a checkerboard.\n",
	     path.c_str());

	ImageInitialData initial = {};
	static const uint32_t checkerboard[] = {
		0xffffffffu, 0xffffffffu, 0xff000000u, 0xff000000u,
		0xffffffffu, 0xffffffffu, 0xff000000u, 0xff000000u,
		0xff000000u, 0xff000000u, 0xffffffffu, 0xffffffffu,
		0xff000000u, 0xff000000u, 0xffffffffu, 0xffffffffu,
	};
	initial.data = checkerboard;

	auto info = ImageCreateInfo::immutable_2d_image(4, 4, VK_FORMAT_R8G8B8A8_UNORM, false);
	info.misc = IMAGE_MISC_CONCURRENT_QUEUE_GRAPHICS_BIT | IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_GRAPHICS_BIT |
	            IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_COMPUTE_BIT;

	auto image = device->create_image(info, &initial);
	if (image)
		device->set_name(*image, path.c_str());
	replace_image(image);
}

void Texture::update_gtx(const MemoryMappedTexture &mapped_file)
{
	if (mapped_file.empty())
	{
		update_checkerboard();
		return;
	}

	auto &layout = mapped_file.get_layout();

	VkComponentMapping swizzle = {};
	mapped_file.remap_swizzle(swizzle);

	Vulkan::ImageHandle image;
	if (!device->image_format_is_supported(layout.get_format(), VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) &&
	    format_compression_type(layout.get_format()) != FormatCompressionType::Uncompressed)
	{
		LOGI("Compressed format #%u is not supported, falling back to compute decode of compressed image.\n",
		     unsigned(layout.get_format()));

		GRANITE_SCOPED_TIMELINE_EVENT_FILE(device->get_system_handles().timeline_trace_file, "texture-load-submit-decompress");
		auto cmd = device->request_command_buffer(CommandBuffer::Type::AsyncCompute);
		image = Granite::decode_compressed_image(*cmd, layout, VK_FORMAT_UNDEFINED, swizzle);
		Semaphore sem;
		device->submit(cmd, nullptr, 1, &sem);
		device->add_wait_semaphore(CommandBuffer::Type::Generic, sem, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, true);
	}
	else
	{
		ImageCreateInfo info = ImageCreateInfo::immutable_image(layout);
		info.swizzle = swizzle;
		info.flags = (mapped_file.get_flags() & MEMORY_MAPPED_TEXTURE_CUBE_MAP_COMPATIBLE_BIT) ?
                         VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT :
                         0;
		info.misc = IMAGE_MISC_CONCURRENT_QUEUE_GRAPHICS_BIT | IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_GRAPHICS_BIT |
		            IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_COMPUTE_BIT;

		if (info.levels == 1 &&
		    (mapped_file.get_flags() & MEMORY_MAPPED_TEXTURE_GENERATE_MIPMAP_ON_LOAD_BIT) != 0 &&
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

		InitialImageBuffer staging;

		{
			GRANITE_SCOPED_TIMELINE_EVENT_FILE(device->get_system_handles().timeline_trace_file,
			                                   "texture-load-create-staging");
			staging = device->create_image_staging_buffer(layout);
		}

		{
			GRANITE_SCOPED_TIMELINE_EVENT_FILE(device->get_system_handles().timeline_trace_file,
			                                   "texture-load-allocate-image");
			image = device->create_image_from_staging_buffer(info, &staging);
		}
	}

	if (image)
		device->set_name(*image, path.c_str());
	replace_image(image);
}

void Texture::update_gtx(Granite::FileMappingHandle file)
{
	MemoryMappedTexture mapped_file;
	if (!mapped_file.map_read(std::move(file)))
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
#ifndef GRANITE_SHIPPING
	deinit();
#endif
	handle.reset();
}

void Texture::replace_image(ImageHandle handle_)
{
	auto old = this->handle.write_object(std::move(handle_));
	if (old)
		device->keep_handle_alive(std::move(old));
}

Image *Texture::get_image()
{
	auto ret = handle.get();
	VK_ASSERT(ret);
	return ret;
}

TextureManager::TextureManager(Device *device_)
	: device(device_)
{
}

Texture *TextureManager::request_texture(const std::string &path, VkFormat format)
{
	Util::Hasher hasher;
	hasher.string(path);
	hasher.u32(format);
	auto hash = hasher.get();

	auto *ret = textures.find(hash);
	if (ret)
		return ret;

	ret = textures.emplace_yield(hash, device, path, format);
	if (!ret->init_texture())
		ret->update_checkerboard();
	return ret;
}
}
