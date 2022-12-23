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

#define NOMINMAX
#include "resource_manager.hpp"
#include "device.hpp"
#include "memory_mapped_texture.hpp"
#include "texture_files.hpp"
#include "texture_decoder.hpp"
#include "string_helpers.hpp"
#include "thread_group.hpp"

namespace Vulkan
{
ResourceManager::ResourceManager(Device *device_)
	: device(device_)
{
}

ResourceManager::~ResourceManager()
{
	// Also works as a teardown mechanism to make sure there are no async threads in flight.
	if (manager)
		manager->set_asset_instantiator_interface(nullptr);
}

void ResourceManager::set_id_bounds(uint32_t bound)
{
	textures.resize(bound);
	views.resize(bound);
}

void ResourceManager::set_image_class(Granite::ImageAssetID id, Granite::ImageClass image_class)
{
	if (id)
	{
		textures[id.id].image_class = image_class;
		if (!views[id.id])
			views[id.id] = &get_fallback_image(image_class)->get_view();
	}
}

void ResourceManager::release_image_resource(Granite::ImageAssetID id)
{
	if (id)
		textures[id.id].image.reset();
}

uint64_t ResourceManager::estimate_cost_image_resource(Granite::ImageAssetID, Granite::File &file)
{
	// TODO: When we get compressed BC/ASTC, this will have to change.
	return file.get_size();
}

void ResourceManager::init()
{
	manager = device->get_system_handles().asset_manager;

	// Need to initialize these before setting the interface.
	{
		uint8_t buffer[4] = {0xff, 0x00, 0xff, 0xff};
		auto info = ImageCreateInfo::immutable_2d_image(1, 1, VK_FORMAT_R8G8B8A8_UNORM);
		info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
		info.misc = IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_COMPUTE_BIT |
		            IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_TRANSFER_BIT |
		            IMAGE_MISC_CONCURRENT_QUEUE_GRAPHICS_BIT |
		            IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_GRAPHICS_BIT;
		ImageInitialData data = {buffer, 0, 0};
		fallback_color = device->create_image(info, &data);
		buffer[0] = 0x80;
		buffer[1] = 0x80;
		buffer[2] = 0xff;
		fallback_normal = device->create_image(info, &data);
		buffer[0] = 0x00;
		buffer[1] = 0x00;
		fallback_pbr = device->create_image(info, &data);
		memset(buffer, 0, sizeof(buffer));
		fallback_zero = device->create_image(info, &data);
	}

	if (manager)
	{
		manager->set_asset_instantiator_interface(this);

		HeapBudget budget[VK_MAX_MEMORY_HEAPS] = {};
		device->get_memory_budget(budget);

		// Try to set aside 50% of budgetable VRAM for the texture manager. Seems reasonable.
		VkDeviceSize size = 0;
		for (uint32_t i = 0; i < device->get_memory_properties().memoryHeapCount; i++)
			if ((device->get_memory_properties().memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0)
				size = std::max(size, budget[i].budget_size / 2);

		if (size == 0)
		{
			LOGW("No DEVICE_LOCAL heap was found, assuming 2 GiB budget.\n");
			size = 2 * 1024 * 1024;
		}

		LOGI("Using texture budget of %u MiB.\n", unsigned(size / (1024 * 1024)));
		manager->set_image_budget(size);

		// This is somewhat arbitrary.
		manager->set_image_budget_per_iteration(2 * 1000 * 1000);
	}
}

ImageHandle ResourceManager::create_gtx(const MemoryMappedTexture &mapped_file, Granite::ImageAssetID id)
{
	if (mapped_file.empty())
		return {};

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
			return {};
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
	{
		auto name = Util::join("ImageAssetID-", id.id);
		device->set_name(*image, name.c_str());
	}
	return image;
}

ImageHandle ResourceManager::create_gtx(Granite::FileMappingHandle mapping, Granite::ImageAssetID id)
{
	MemoryMappedTexture mapped_file;
	if (!mapped_file.map_read(std::move(mapping)))
	{
		LOGE("Failed to read texture.\n");
		return {};
	}

	return create_gtx(mapped_file, id);
}

ImageHandle ResourceManager::create_other(const Granite::FileMapping &mapping, Granite::ImageClass image_class,
                                          Granite::ImageAssetID id)
{
	auto tex = load_texture_from_memory(mapping.data(),
	                                    mapping.get_size(), image_class == Granite::ImageClass::Color ?
	                                                        ColorSpace::sRGB : ColorSpace::Linear);
	return create_gtx(tex, id);
}

const Vulkan::ImageView *ResourceManager::get_image_view_blocking(Granite::ImageAssetID id)
{
	std::unique_lock<std::mutex> holder{lock};

	if (id.id >= textures.size())
	{
		LOGE("ID %u is out of bounds.\n", id.id);
		return nullptr;
	}

	if (textures[id.id].image)
		return &textures[id.id].image->get_view();

	if (!manager->iterate_blocking(*device->get_system_handles().thread_group, id))
	{
		LOGE("Failed to iterate.\n");
		return nullptr;
	}

	cond.wait(holder, [this, id]() -> bool {
		return bool(textures[id.id].image);
	});

	return &textures[id.id].image->get_view();
}

void ResourceManager::instantiate_image_resource(Granite::AssetManager &manager_, Granite::TaskGroup *task,
                                                 Granite::ImageAssetID id, Granite::File &file)
{
	if (task)
	{
		task->enqueue_task([this, &manager_, &file, id]() {
			instantiate_image_resource(manager_, id, file);
		});
	}
	else
	{
		instantiate_image_resource(manager_, id, file);
	}
}

void ResourceManager::instantiate_image_resource(Granite::AssetManager &manager_,
                                                 Granite::ImageAssetID id,
                                                 Granite::File &file)
{
	ImageHandle image;
	if (file.get_size())
	{
		auto mapping = file.map();
		if (mapping)
		{
			if (MemoryMappedTexture::is_header(mapping->data(), mapping->get_size()))
				image = create_gtx(std::move(mapping), id);
			else
				image = create_other(*mapping, textures[id.id].image_class, id);
		}
		else
			LOGE("Failed to map file.\n");
	}

	manager_.update_cost(id, image ? image->get_allocation().get_size() : 0);

	// Have to signal something.
	if (!image)
		image = get_fallback_image(textures[id.id].image_class);

	std::lock_guard<std::mutex> holder{lock};
	updates.push_back(id);
	textures[id.id].image = std::move(image);
	cond.notify_all();
}

const ImageHandle &ResourceManager::get_fallback_image(Granite::ImageClass image_class)
{
	switch (image_class)
	{
	default:
	case Granite::ImageClass::Zeroable:
		return fallback_zero;
	case Granite::ImageClass::Color:
		return fallback_color;
	case Granite::ImageClass::Normal:
		return fallback_normal;
	case Granite::ImageClass::MetallicRoughness:
		return fallback_pbr;
	}
}

void ResourceManager::latch_handles()
{
	std::lock_guard<std::mutex> holder{lock};
	for (auto &update : updates)
	{
		if (update.id >= views.size())
			continue;

		const ImageView *view;

		if (textures[update.id].image)
		{
			view = &textures[update.id].image->get_view();
		}
		else
		{
			auto &img = get_fallback_image(textures[update.id].image_class);
			view = &img->get_view();
		}

		views[update.id] = view;
	}
	updates.clear();
}
}
