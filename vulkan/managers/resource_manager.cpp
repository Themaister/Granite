/* Copyright (c) 2017-2023 Hans-Kristian Arntzen
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
	, index_buffer_allocator(*device_)
	, position_buffer_allocator(*device_)
	, attribute_buffer_allocator(*device_)
{
	// Simplified style.
	index_buffer_allocator.set_element_size(sizeof(uint32_t) * 3);
	position_buffer_allocator.set_element_size(sizeof(float) * 3);
	attribute_buffer_allocator.set_element_size(sizeof(float) * 2 + sizeof(uint32_t) * 2);
}

ResourceManager::~ResourceManager()
{
	// Also works as a teardown mechanism to make sure there are no async threads in flight.
	if (manager)
		manager->set_asset_instantiator_interface(nullptr);
}

void ResourceManager::set_id_bounds(uint32_t bound)
{
	assets.resize(bound);
	views.resize(bound);
}

void ResourceManager::set_asset_class(Granite::AssetID id, Granite::AssetClass asset_class)
{
	if (id)
	{
		assets[id.id].asset_class = asset_class;
		if (!views[id.id])
			views[id.id] = &get_fallback_image(asset_class)->get_view();
	}
}

void ResourceManager::release_asset(Granite::AssetID id)
{
	if (id)
		assets[id.id].image.reset();
}

uint64_t ResourceManager::estimate_cost_asset(Granite::AssetID, Granite::File &file)
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
		manager->set_asset_budget(size);

		// This is somewhat arbitrary.
		manager->set_asset_budget_per_iteration(2 * 1000 * 1000);
	}
}

ImageHandle ResourceManager::create_gtx(const MemoryMappedTexture &mapped_file, Granite::AssetID id)
{
	if (mapped_file.empty())
		return {};

	auto &layout = mapped_file.get_layout();

	VkComponentMapping swizzle = {};
	mapped_file.remap_swizzle(swizzle);

	ImageHandle image;
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
		device->add_wait_semaphore(CommandBuffer::Type::Generic, sem, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, true);
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
		auto name = Util::join("AssetID-", id.id);
		device->set_name(*image, name.c_str());
	}
	return image;
}

ImageHandle ResourceManager::create_gtx(Granite::FileMappingHandle mapping, Granite::AssetID id)
{
	MemoryMappedTexture mapped_file;
	if (!mapped_file.map_read(std::move(mapping)))
	{
		LOGE("Failed to read texture.\n");
		return {};
	}

	return create_gtx(mapped_file, id);
}

ImageHandle ResourceManager::create_other(const Granite::FileMapping &mapping, Granite::AssetClass asset_class,
                                          Granite::AssetID id)
{
	auto tex = load_texture_from_memory(mapping.data(),
	                                    mapping.get_size(), asset_class == Granite::AssetClass::ImageColor ?
	                                                        ColorSpace::sRGB : ColorSpace::Linear);
	return create_gtx(tex, id);
}

const ImageView *ResourceManager::get_image_view_blocking(Granite::AssetID id)
{
	std::unique_lock<std::mutex> holder{lock};

	if (id.id >= assets.size())
	{
		LOGE("ID %u is out of bounds.\n", id.id);
		return nullptr;
	}

	if (assets[id.id].image)
		return &assets[id.id].image->get_view();

	if (!manager->iterate_blocking(*device->get_system_handles().thread_group, id))
	{
		LOGE("Failed to iterate.\n");
		return nullptr;
	}

	cond.wait(holder, [this, id]() -> bool {
		return bool(assets[id.id].image);
	});

	return &assets[id.id].image->get_view();
}

void ResourceManager::instantiate_asset(Granite::AssetManager &manager_, Granite::TaskGroup *task,
                                        Granite::AssetID id, Granite::File &file)
{
	if (task)
	{
		task->enqueue_task([this, &manager_, &file, id]() {
			instantiate_asset(manager_, id, file);
		});
	}
	else
	{
		instantiate_asset(manager_, id, file);
	}
}

void ResourceManager::instantiate_asset(Granite::AssetManager &manager_,
                                        Granite::AssetID id,
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
				image = create_other(*mapping, assets[id.id].asset_class, id);
		}
		else
			LOGE("Failed to map file.\n");
	}

	manager_.update_cost(id, image ? image->get_allocation().get_size() : 0);

	// Have to signal something.
	if (!image)
		image = get_fallback_image(assets[id.id].asset_class);

	std::lock_guard<std::mutex> holder{lock};
	updates.push_back(id);
	assets[id.id].image = std::move(image);
	cond.notify_all();
}

const ImageHandle &ResourceManager::get_fallback_image(Granite::AssetClass asset_class)
{
	switch (asset_class)
	{
	default:
	case Granite::AssetClass::ImageZeroable:
		return fallback_zero;
	case Granite::AssetClass::ImageColor:
		return fallback_color;
	case Granite::AssetClass::ImageNormal:
		return fallback_normal;
	case Granite::AssetClass::ImageMetallicRoughness:
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

		if (assets[update.id].image)
		{
			view = &assets[update.id].image->get_view();
		}
		else
		{
			auto &img = get_fallback_image(assets[update.id].asset_class);
			view = &img->get_view();
		}

		views[update.id] = view;
	}
	updates.clear();
}

const Buffer *ResourceManager::get_index_buffer() const
{
	return index_buffer_allocator.get_buffer(0);
}

const Buffer *ResourceManager::get_position_buffer() const
{
	return position_buffer_allocator.get_buffer(0);
}

const Buffer *ResourceManager::get_attribute_buffer() const
{
	return attribute_buffer_allocator.get_buffer(0);
}

MeshBufferAllocator::MeshBufferAllocator(Device &device)
	: global_allocator(device)
{
	for (int i = 0; i < SliceAllocatorCount - 1; i++)
		allocators[i].parent = &allocators[i + 1];
	allocators[SliceAllocatorCount - 1].global_allocator = &global_allocator;

	// Basic unit of a meshlet is 256 prims / attributes.
	// Maximum element count = 32M prims.
	allocators[0].sub_block_size = 256;
	for (int i = 1; i < SliceAllocatorCount; i++)
		allocators[i].sub_block_size = allocators[i - 1].sub_block_size * (Util::LegionAllocator::NumSubBlocks / 2);
}

void MeshBufferAllocator::set_element_size(uint32_t element_size)
{
	global_allocator.set_element_size(element_size);
}

const Buffer *MeshBufferAllocator::get_buffer(unsigned index) const
{
	if (index < global_allocator.global_buffers.size())
		return global_allocator.global_buffers[index].get();
	else
		return nullptr;
}

namespace Internal
{
uint32_t MeshGlobalAllocator::allocate(uint32_t count)
{
	BufferCreateInfo info = {};
	info.size = VkDeviceSize(count) * element_size;
	info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
	             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
	             VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	info.domain = BufferDomain::Device;
	auto buf = device.create_buffer(info);

	for (uint32_t i = 0, n = global_buffers.size(); i < n; i++)
	{
		if (!global_buffers[i])
		{
			global_buffers[i] = std::move(buf);
			return i;
		}
	}

	// For now, have one global buffer for VBO / IBO.
	if (!global_buffers.empty())
		return UINT32_MAX;

	uint32_t ret = global_buffers.size();
	global_buffers.push_back(std::move(buf));
	return ret;
}

void MeshGlobalAllocator::set_element_size(uint32_t element_size_)
{
	element_size = element_size_;
}

void MeshGlobalAllocator::free(uint32_t index)
{
	VK_ASSERT(index < global_buffers.size());
	global_buffers[index].reset();
}

MeshGlobalAllocator::MeshGlobalAllocator(Device &device_)
	: device(device_)
{}

bool SliceAllocator::allocate_backing_heap(AllocatedSlice *allocation)
{
	uint32_t count = sub_block_size * Util::LegionAllocator::NumSubBlocks;

	if (parent)
	{
		return parent->allocate(count, allocation);
	}
	else if (global_allocator)
	{
		uint32_t index = global_allocator->allocate(count);
		if (index == UINT32_MAX)
			return false;

		*allocation = {};
		allocation->count = count;
		allocation->buffer_index = index;
		return true;
	}
	else
	{
		return false;
	}
}

void SliceAllocator::free_backing_heap(AllocatedSlice *allocation) const
{
	if (parent)
		parent->free(allocation->heap, allocation->mask);
	else if (global_allocator)
		global_allocator->free(allocation->buffer_index);
}

void SliceAllocator::prepare_allocation(AllocatedSlice *allocation, Util::IntrusiveList<MiniHeap>::Iterator heap,
                                        const Util::SuballocationResult &suballoc)
{
	allocation->buffer_index = heap->allocation.buffer_index;
	allocation->offset = heap->allocation.offset + suballoc.offset;
	allocation->count = suballoc.size;
	allocation->mask = suballoc.mask;
	allocation->heap = heap;
	allocation->alloc = this;
}
}

bool MeshBufferAllocator::allocate(uint32_t count, Internal::AllocatedSlice *slice)
{
	for (auto &alloc : allocators)
		if (count <= alloc.get_max_allocation_size())
			return alloc.allocate(count, slice);

	LOGE("Allocation of %u elements is too large for MeshBufferAllocator.\n", count);
	return false;
}

void MeshBufferAllocator::free(const Internal::AllocatedSlice &slice)
{
	if (slice.alloc)
		slice.alloc->free(slice.heap, slice.mask);
	else
		global_allocator.free(slice.buffer_index);
}
}
