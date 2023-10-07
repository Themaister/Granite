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
#include "meshlet.hpp"

namespace Vulkan
{
ResourceManager::ResourceManager(Device *device_)
	: device(device_)
	, index_buffer_allocator(*device_)
	, attribute_buffer_allocator(*device_)
	, indirect_buffer_allocator(*device_)
{
	// Simplified style.
	index_buffer_allocator.set_element_size(0, sizeof(uint32_t) * 3);
	attribute_buffer_allocator.set_soa_count(3);
	attribute_buffer_allocator.set_element_size(0, sizeof(float) * 3);
	attribute_buffer_allocator.set_element_size(1, sizeof(float) * 2 + sizeof(uint32_t) * 2);
	attribute_buffer_allocator.set_element_size(2, sizeof(uint32_t) * 2);
	indirect_buffer_allocator.set_element_size(0, sizeof(VkDrawIndexedIndirectCommand));
	assets.reserve(Granite::AssetID::MaxIDs);
}

ResourceManager::~ResourceManager()
{
	// Also works as a teardown mechanism to make sure there are no async threads in flight.
	if (manager)
		manager->set_asset_instantiator_interface(nullptr);
}

void ResourceManager::set_id_bounds(uint32_t bound)
{
	// We must avoid reallocation here to avoid a ton of extra silly locking.
	VK_ASSERT(bound <= Granite::AssetID::MaxIDs);
	assets.resize(bound);
}

void ResourceManager::set_asset_class(Granite::AssetID id, Granite::AssetClass asset_class)
{
	if (id)
	{
		assets[id.id].asset_class = asset_class;
		if (asset_class != Granite::AssetClass::Mesh)
		{
			std::unique_lock<std::mutex> holder{lock};
			views.resize(assets.size());

			if (!views[id.id])
				views[id.id] = &get_fallback_image(asset_class)->get_view();
		}
	}
}

void ResourceManager::release_asset(Granite::AssetID id)
{
	if (id)
	{
		std::unique_lock<std::mutex> holder{lock};
		VK_ASSERT(id.id < assets.size());
		auto &asset = assets[id.id];
		asset.latchable = false;
		updates.push_back(id);
	}
}

uint64_t ResourceManager::estimate_cost_asset(Granite::AssetID id, Granite::File &file)
{
	if (assets[id.id].asset_class == Granite::AssetClass::Mesh)
	{
		// Compression factor of 2x is reasonable to assume.
		return file.get_size() * 2;
	}
	else
	{
		// TODO: When we get compressed BC/ASTC, this will have to change.
		return file.get_size();
	}
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

		// Try to set aside 50% of budgetable VRAM for the resource manager. Seems reasonable.
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

	auto &asset = assets[id.id];

	if (asset.image)
		return &asset.image->get_view();

	if (!manager->iterate_blocking(*device->get_system_handles().thread_group, id))
	{
		LOGE("Failed to iterate.\n");
		return nullptr;
	}

	cond.wait(holder, [&asset]() -> bool {
		return bool(asset.latchable);
	});

	return &asset.image->get_view();
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
	auto &asset = assets[id.id];
	if (asset.asset_class == Granite::AssetClass::Mesh)
		instantiate_asset_mesh(manager_, id, file);
	else
		instantiate_asset_image(manager_, id, file);
}

bool ResourceManager::allocate_asset_mesh(Granite::AssetID id, const Meshlet::MeshView &view)
{
	if (!view.format_header)
		return false;

	Internal::AllocatedSlice index_slice, attribute_slice;
	{
		std::lock_guard<std::mutex> holder{mesh_allocator_lock};
		if (!index_buffer_allocator.allocate(view.total_primitives, &index_slice))
			return false;

		if (!attribute_buffer_allocator.allocate(view.total_vertices, &attribute_slice))
		{
			index_buffer_allocator.free(index_slice);
			return false;
		}
	}

	auto &asset = assets[id.id];
	asset.mesh.index = index_slice;
	asset.mesh.attr = attribute_slice;
	return true;
}

void ResourceManager::instantiate_asset_mesh(Granite::AssetManager &manager_,
                                             Granite::AssetID id,
                                             Granite::File &file)
{
	Granite::FileMappingHandle mapping;
	if (file.get_size())
		mapping = file.map();

	Meshlet::MeshView view = {};
	if (mapping)
		view = Meshlet::create_mesh_view(*mapping);
	bool ret = allocate_asset_mesh(id, view);

	// Decode the meshlet. Later, we'll have to do a lot of device specific stuff here to select optimal
	// processing:
	// - Native meshlets
	// - Encoded attribute
	// - Decoded attributes
	// - Optimize for multi-draw-indirect or not? (8-bit indices).

	auto &asset = assets[id.id];

	if (ret)
	{
		auto cmd = device->request_command_buffer(CommandBuffer::Type::AsyncCompute);

		BufferCreateInfo buf = {};
		buf.domain = BufferDomain::Host;
		buf.size = view.format_header->payload_size_words * sizeof(uint32_t);
		buf.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		auto payload = device->create_buffer(buf, view.payload);

		Meshlet::DecodeInfo info = {};
		info.target_style = Meshlet::MeshStyle::Textured;
		info.ibo = {
			index_buffer_allocator.get_buffer(0, 0),
			asset.mesh.index.offset * index_buffer_allocator.get_element_size(0),
		};

		for (unsigned i = 0; i < 3; i++)
		{
			info.streams[i] = {
				attribute_buffer_allocator.get_buffer(0, i),
				asset.mesh.index.offset * attribute_buffer_allocator.get_element_size(i),
			};
		}

		info.payload = { payload.get(), 0 };

		Meshlet::decode_mesh(*cmd, info, view);

		Semaphore sem[2];
		device->submit(cmd, nullptr, 2, sem);
		device->add_wait_semaphore(CommandBuffer::Type::Generic, std::move(sem[0]),
		                           VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT |
		                           VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT, false);
		device->add_wait_semaphore(CommandBuffer::Type::AsyncGraphics, std::move(sem[1]),
		                           VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT |
		                           VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT, false);
	}

	uint64_t cost = 0;
	if (ret)
	{
		cost += view.total_primitives * index_buffer_allocator.get_element_size(0);
		cost += view.total_vertices * attribute_buffer_allocator.get_element_size(0);
		cost += view.total_vertices * attribute_buffer_allocator.get_element_size(1);
		cost += view.total_vertices * attribute_buffer_allocator.get_element_size(2);
		cost += view.format_header->meshlet_count * indirect_buffer_allocator.get_element_size(0);
	}

	std::lock_guard<std::mutex> holder{lock};
	updates.push_back(id);
	manager_.update_cost(id, ret ? cost : 0);
	asset.latchable = true;
	cond.notify_all();
}

void ResourceManager::instantiate_asset_image(Granite::AssetManager &manager_,
                                              Granite::AssetID id,
                                              Granite::File &file)
{
	auto &asset = assets[id.id];

	ImageHandle image;
	if (file.get_size())
	{
		auto mapping = file.map();
		if (mapping)
		{
			if (MemoryMappedTexture::is_header(mapping->data(), mapping->get_size()))
				image = create_gtx(std::move(mapping), id);
			else
				image = create_other(*mapping, asset.asset_class, id);
		}
		else
			LOGE("Failed to map file.\n");
	}

	// Have to signal something.
	if (!image)
		image = get_fallback_image(asset.asset_class);

	std::lock_guard<std::mutex> holder{lock};
	updates.push_back(id);
	asset.image = std::move(image);
	asset.latchable = true;
	manager_.update_cost(id, asset.image ? asset.image->get_allocation().get_size() : 0);
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

	views.resize(assets.size());
	draws.resize(assets.size());

	for (auto &update : updates)
	{
		if (update.id >= views.size())
			continue;
		auto &asset = assets[update.id];

		if (asset.asset_class == Granite::AssetClass::Mesh)
		{
			if (!asset.latchable)
			{
				{
					std::lock_guard<std::mutex> holder_alloc{mesh_allocator_lock};
					index_buffer_allocator.free(asset.mesh.index);
					attribute_buffer_allocator.free(asset.mesh.attr);
					indirect_buffer_allocator.free(asset.mesh.indirect);
				}
				asset.mesh = {};
			}

			auto &d = draws[update.id];
			d.firstIndex = asset.mesh.index.offset * 3;
			d.indexCount = asset.mesh.index.count * 3;
			d.firstInstance = 0;
			d.instanceCount = 1;
			d.vertexOffset = int32_t(asset.mesh.attr.offset);
		}
		else
		{
			const ImageView *view;
			if (!asset.latchable)
				asset.image.reset();

			if (asset.image)
			{
				view = &asset.image->get_view();
			}
			else
			{
				auto &img = get_fallback_image(asset.asset_class);
				view = &img->get_view();
			}

			views[update.id] = view;
		}
	}
	updates.clear();
}

const Buffer *ResourceManager::get_index_buffer() const
{
	return index_buffer_allocator.get_buffer(0, 0);
}

const Buffer *ResourceManager::get_position_buffer() const
{
	return attribute_buffer_allocator.get_buffer(0, 0);
}

const Buffer *ResourceManager::get_attribute_buffer() const
{
	return attribute_buffer_allocator.get_buffer(0, 1);
}

const Buffer *ResourceManager::get_skinning_buffer() const
{
	return attribute_buffer_allocator.get_buffer(0, 2);
}

const Buffer *ResourceManager::get_indirect_buffer() const
{
	return indirect_buffer_allocator.get_buffer(0, 0);
}

MeshBufferAllocator::MeshBufferAllocator(Device &device)
	: global_allocator(device)
{
	for (int i = 0; i < SliceAllocatorCount - 1; i++)
		allocators[i].parent = &allocators[i + 1];
	allocators[SliceAllocatorCount - 1].global_allocator = &global_allocator;

	// Basic unit of a meshlet is 256 prims / attributes.
	// Maximum element count = 32M prims.
	allocators[0].set_sub_block_size(256);
	for (int i = 1; i < SliceAllocatorCount; i++)
		allocators[i].set_sub_block_size(allocators[i - 1].get_sub_block_size() * (Util::LegionAllocator::NumSubBlocks / 2));

	for (auto &alloc : allocators)
		alloc.set_object_pool(&object_pool);
}

void MeshBufferAllocator::set_soa_count(unsigned soa_count)
{
	VK_ASSERT(soa_count <= Internal::MeshGlobalAllocator::MaxSoACount);
	global_allocator.soa_count = soa_count;
}

void MeshBufferAllocator::set_element_size(unsigned soa_index, uint32_t element_size)
{
	VK_ASSERT(soa_index < global_allocator.soa_count);
	global_allocator.element_size[soa_index] = element_size;
}

uint32_t MeshBufferAllocator::get_element_size(unsigned soa_index) const
{
	VK_ASSERT(soa_index < global_allocator.soa_count);
	return global_allocator.element_size[soa_index];
}

const Buffer *MeshBufferAllocator::get_buffer(unsigned index, unsigned soa_index) const
{
	VK_ASSERT(soa_index < global_allocator.soa_count);
	index = index * global_allocator.soa_count + soa_index;

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

	uint32_t target_index = UINT32_MAX;
	uint32_t search_index = 0;

	for (uint32_t i = 0, n = global_buffers.size(); i < n; i += soa_count, search_index++)
	{
		if (!global_buffers[i])
		{
			target_index = search_index;
			break;
		}
	}

	if (target_index == UINT32_MAX)
	{
		if (!global_buffers.empty())
			return UINT32_MAX;

		target_index = search_index;
		for (uint32_t i = 0; i < soa_count; i++)
			global_buffers.emplace_back();
	}

	for (uint32_t soa_index = 0; soa_index < soa_count; soa_index++)
	{
		info.size = VkDeviceSize(count) * element_size[soa_index];
		info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
		             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
		             VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
		             VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
		info.domain = BufferDomain::Device;
		global_buffers[target_index * soa_count + soa_index] = device.create_buffer(info);
	}

	return target_index;
}

void MeshGlobalAllocator::free(uint32_t index)
{
	index *= soa_count;
	VK_ASSERT(index < global_buffers.size());
	for (uint32_t i = 0; i < soa_count; i++)
		global_buffers[index + i].reset();
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
	{
		uint32_t max_alloc_size = alloc.get_max_allocation_size();
		if (count <= max_alloc_size)
			return alloc.allocate(count, slice);
	}

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
