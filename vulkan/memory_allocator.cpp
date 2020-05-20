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

#include "memory_allocator.hpp"
#include "device.hpp"
#include <algorithm>

using namespace std;

#ifdef GRANITE_VULKAN_MT
#define ALLOCATOR_LOCK() std::lock_guard<std::mutex> holder__{lock}
#else
#define ALLOCATOR_LOCK()
#endif

namespace Vulkan
{

void DeviceAllocation::free_immediate()
{
	if (!alloc)
		return;

	alloc->free(this);
	alloc = nullptr;
	base = VK_NULL_HANDLE;
	mask = 0;
	offset = 0;
}

void DeviceAllocation::free_immediate(DeviceAllocator &allocator)
{
	if (alloc)
		free_immediate();
	else if (base)
	{
		allocator.free_no_recycle(size, memory_type, base, host_base);
		base = VK_NULL_HANDLE;
	}
}

void DeviceAllocation::free_global(DeviceAllocator &allocator, uint32_t size_, uint32_t memory_type_)
{
	if (base)
	{
		allocator.free(size_, memory_type_, base, host_base);
		base = VK_NULL_HANDLE;
		mask = 0;
		offset = 0;
	}
}

void Block::allocate(uint32_t num_blocks, DeviceAllocation *block)
{
	VK_ASSERT(NumSubBlocks >= num_blocks);
	VK_ASSERT(num_blocks != 0);

	uint32_t block_mask;
	if (num_blocks == NumSubBlocks)
		block_mask = ~0u;
	else
		block_mask = ((1u << num_blocks) - 1u);

	uint32_t mask = free_blocks[num_blocks - 1];
	uint32_t b = trailing_zeroes(mask);

	VK_ASSERT(((free_blocks[0] >> b) & block_mask) == block_mask);

	uint32_t sb = block_mask << b;
	free_blocks[0] &= ~sb;
	update_longest_run();

	block->mask = sb;
	block->offset = b;
}

void Block::free(uint32_t mask)
{
	VK_ASSERT((free_blocks[0] & mask) == 0);
	free_blocks[0] |= mask;
	update_longest_run();
}

void ClassAllocator::suballocate(uint32_t num_blocks, uint32_t tiling, uint32_t memory_type_, MiniHeap &heap,
                                 DeviceAllocation *alloc)
{
	heap.heap.allocate(num_blocks, alloc);
	alloc->base = heap.allocation.base;
	alloc->offset <<= sub_block_size_log2;

	if (heap.allocation.host_base)
		alloc->host_base = heap.allocation.host_base + alloc->offset;

	alloc->offset += heap.allocation.offset;
	alloc->tiling = tiling;
	alloc->memory_type = memory_type_;
	alloc->alloc = this;
	alloc->size = num_blocks << sub_block_size_log2;
}

bool ClassAllocator::allocate(uint32_t size, AllocationTiling tiling, DeviceAllocation *alloc, bool hierarchical)
{
	ALLOCATOR_LOCK();
	unsigned num_blocks = (size + sub_block_size - 1) >> sub_block_size_log2;
	uint32_t size_mask = (1u << (num_blocks - 1)) - 1;
	uint32_t masked_tiling_mode = tiling_mask & tiling;
	auto &m = tiling_modes[masked_tiling_mode];

	uint32_t index = trailing_zeroes(m.heap_availability_mask & ~size_mask);

	if (index < Block::NumSubBlocks)
	{
		auto itr = m.heaps[index].begin();
		VK_ASSERT(itr);
		VK_ASSERT(index >= (num_blocks - 1));

		auto &heap = *itr;
		suballocate(num_blocks, masked_tiling_mode, memory_type, heap, alloc);
		unsigned new_index = heap.heap.get_longest_run() - 1;

		if (heap.heap.full())
		{
			m.full_heaps.move_to_front(m.heaps[index], itr);
			if (!m.heaps[index].begin())
				m.heap_availability_mask &= ~(1u << index);
		}
		else if (new_index != index)
		{
			auto &new_heap = m.heaps[new_index];
			new_heap.move_to_front(m.heaps[index], itr);
			m.heap_availability_mask |= 1u << new_index;
			if (!m.heaps[index].begin())
				m.heap_availability_mask &= ~(1u << index);
		}

		alloc->heap = itr;
		alloc->hierarchical = hierarchical;

		return true;
	}

	// We didn't find a vacant heap, make a new one.
	auto *node = object_pool.allocate();
	if (!node)
		return false;

	auto &heap = *node;
	uint32_t alloc_size = sub_block_size * Block::NumSubBlocks;

	if (parent)
	{
		// We cannot allocate a new block from parent ... This is fatal.
		if (!parent->allocate(alloc_size, tiling, &heap.allocation, true))
		{
			object_pool.free(node);
			return false;
		}
	}
	else
	{
		heap.allocation.offset = 0;
		if (!global_allocator->allocate(alloc_size, memory_type, &heap.allocation.base, &heap.allocation.host_base,
		                                VK_NULL_HANDLE))
		{
			object_pool.free(node);
			return false;
		}
	}

	// This cannot fail.
	suballocate(num_blocks, masked_tiling_mode, memory_type, heap, alloc);

	alloc->heap = node;
	if (heap.heap.full())
	{
		m.full_heaps.insert_front(node);
	}
	else
	{
		unsigned new_index = heap.heap.get_longest_run() - 1;
		m.heaps[new_index].insert_front(node);
		m.heap_availability_mask |= 1u << new_index;
	}

	alloc->hierarchical = hierarchical;

	return true;
}

ClassAllocator::~ClassAllocator()
{
	bool error = false;
	for (auto &m : tiling_modes)
	{
		if (m.full_heaps.begin())
			error = true;

		for (auto &h : m.heaps)
			if (h.begin())
				error = true;
	}

	if (error)
		LOGE("Memory leaked in class allocator!\n");
}

void ClassAllocator::free(DeviceAllocation *alloc)
{
	ALLOCATOR_LOCK();
	auto *heap = &*alloc->heap;
	auto &block = heap->heap;
	bool was_full = block.full();
	auto &m = tiling_modes[alloc->tiling];

	unsigned index = block.get_longest_run() - 1;
	block.free(alloc->mask);
	unsigned new_index = block.get_longest_run() - 1;

	if (block.empty())
	{
		// Our mini-heap is completely freed, free to higher level allocator.
		if (parent)
			heap->allocation.free_immediate();
		else
			heap->allocation.free_global(*global_allocator, sub_block_size * Block::NumSubBlocks, memory_type);

		if (was_full)
			m.full_heaps.erase(heap);
		else
		{
			m.heaps[index].erase(heap);
			if (!m.heaps[index].begin())
				m.heap_availability_mask &= ~(1u << index);
		}

		object_pool.free(heap);
	}
	else if (was_full)
	{
		m.heaps[new_index].move_to_front(m.full_heaps, heap);
		m.heap_availability_mask |= 1u << new_index;
	}
	else if (index != new_index)
	{
		m.heaps[new_index].move_to_front(m.heaps[index], heap);
		m.heap_availability_mask |= 1u << new_index;
		if (!m.heaps[index].begin())
			m.heap_availability_mask &= ~(1u << index);
	}
}

bool Allocator::allocate_global(uint32_t size, DeviceAllocation *alloc)
{
	// Fall back to global allocation, do not recycle.
	if (!global_allocator->allocate(size, memory_type, &alloc->base, &alloc->host_base, VK_NULL_HANDLE))
		return false;
	alloc->alloc = nullptr;
	alloc->memory_type = memory_type;
	alloc->size = size;
	return true;
}

bool Allocator::allocate_dedicated(uint32_t size, DeviceAllocation *alloc, VkImage dedicated_image)
{
	// Fall back to global allocation, do not recycle.
	if (!global_allocator->allocate(size, memory_type, &alloc->base, &alloc->host_base, dedicated_image))
		return false;
	alloc->alloc = nullptr;
	alloc->memory_type = memory_type;
	alloc->size = size;
	return true;
}

DeviceAllocation DeviceAllocation::make_imported_allocation(VkDeviceMemory memory, VkDeviceSize size,
                                                            uint32_t memory_type)
{
	DeviceAllocation alloc = {};
	alloc.base = memory;
	alloc.offset = 0;
	alloc.size = size;
	alloc.memory_type = memory_type;
	return alloc;
}

bool Allocator::allocate(uint32_t size, uint32_t alignment, AllocationTiling mode, DeviceAllocation *alloc)
{
	for (auto &c : classes)
	{
		// Find a suitable class to allocate from.
		if (size <= c.sub_block_size * Block::NumSubBlocks)
		{
			if (alignment > c.sub_block_size)
			{
				size_t padded_size = size + (alignment - c.sub_block_size);
				if (padded_size <= c.sub_block_size * Block::NumSubBlocks)
					size = padded_size;
				else
					continue;
			}

			bool ret = c.allocate(size, mode, alloc, false);
			if (ret)
			{
				uint32_t aligned_offset = (alloc->offset + alignment - 1) & ~(alignment - 1);
				if (alloc->host_base)
					alloc->host_base += aligned_offset - alloc->offset;
				alloc->offset = aligned_offset;
			}
			return ret;
		}
	}

	return allocate_global(size, alloc);
}

Allocator::Allocator()
{
	for (unsigned i = 0; i < MEMORY_CLASS_COUNT - 1; i++)
		classes[i].set_parent(&classes[i + 1]);

	get_class_allocator(MEMORY_CLASS_SMALL).set_tiling_mask(~0u);
	get_class_allocator(MEMORY_CLASS_MEDIUM).set_tiling_mask(~0u);
	get_class_allocator(MEMORY_CLASS_LARGE).set_tiling_mask(0);
	get_class_allocator(MEMORY_CLASS_HUGE).set_tiling_mask(0);

	get_class_allocator(MEMORY_CLASS_SMALL).set_sub_block_size(128);
	get_class_allocator(MEMORY_CLASS_MEDIUM).set_sub_block_size(128 * Block::NumSubBlocks); // 4K

	// 128K, this is the largest bufferImageGranularity a Vulkan implementation may have.
	get_class_allocator(MEMORY_CLASS_LARGE).set_sub_block_size(128 * Block::NumSubBlocks * Block::NumSubBlocks);
	get_class_allocator(MEMORY_CLASS_HUGE)
	    .set_sub_block_size(64 * Block::NumSubBlocks * Block::NumSubBlocks * Block::NumSubBlocks); // 2M
}

void DeviceAllocator::init(Device *device_)
{
	device = device_;
	table = &device->get_device_table();
	mem_props = device->get_memory_properties();
	const auto &props = device->get_gpu_properties();
	atom_alignment = props.limits.nonCoherentAtomSize;

	heaps.clear();
	allocators.clear();

	heaps.resize(mem_props.memoryHeapCount);
	allocators.reserve(mem_props.memoryTypeCount);
	for (unsigned i = 0; i < mem_props.memoryTypeCount; i++)
	{
		allocators.emplace_back(new Allocator);
		allocators.back()->set_memory_type(i);
		allocators.back()->set_global_allocator(this);
	}
}

bool DeviceAllocator::allocate(uint32_t size, uint32_t alignment, uint32_t memory_type, AllocationTiling mode,
                               DeviceAllocation *alloc)
{
	return allocators[memory_type]->allocate(size, alignment, mode, alloc);
}

bool DeviceAllocator::allocate_image_memory(uint32_t size, uint32_t alignment, uint32_t memory_type,
                                            AllocationTiling tiling, DeviceAllocation *alloc, VkImage image,
                                            bool force_no_dedicated)
{
	if (!use_dedicated || force_no_dedicated)
		return allocate(size, alignment, memory_type, tiling, alloc);

	VkImageMemoryRequirementsInfo2KHR info = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2_KHR };
	info.image = image;

	VkMemoryDedicatedRequirementsKHR dedicated_req = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS_KHR };
	VkMemoryRequirements2KHR mem_req = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2_KHR };
	mem_req.pNext = &dedicated_req;
	table->vkGetImageMemoryRequirements2KHR(device->get_device(), &info, &mem_req);

	if (dedicated_req.prefersDedicatedAllocation || dedicated_req.requiresDedicatedAllocation)
		return allocators[memory_type]->allocate_dedicated(size, alloc, image);
	else
		return allocate(size, alignment, memory_type, tiling, alloc);
}

bool DeviceAllocator::allocate_global(uint32_t size, uint32_t memory_type, DeviceAllocation *alloc)
{
	return allocators[memory_type]->allocate_global(size, alloc);
}

void DeviceAllocator::Heap::garbage_collect(Device *device_)
{
	auto &table_ = device_->get_device_table();
	for (auto &block : blocks)
	{
		if (block.host_memory)
			table_.vkUnmapMemory(device_->get_device(), block.memory);
		table_.vkFreeMemory(device_->get_device(), block.memory, nullptr);
		size -= block.size;
	}
}

DeviceAllocator::~DeviceAllocator()
{
	for (auto &heap : heaps)
		heap.garbage_collect(device);
}

void DeviceAllocator::free(uint32_t size, uint32_t memory_type, VkDeviceMemory memory, uint8_t *host_memory)
{
	ALLOCATOR_LOCK();
	auto &heap = heaps[mem_props.memoryTypes[memory_type].heapIndex];
	heap.blocks.push_back({ memory, host_memory, size, memory_type });
}

void DeviceAllocator::free_no_recycle(uint32_t size, uint32_t memory_type, VkDeviceMemory memory, uint8_t *host_memory)
{
	ALLOCATOR_LOCK();
	auto &heap = heaps[mem_props.memoryTypes[memory_type].heapIndex];
	if (host_memory)
		table->vkUnmapMemory(device->get_device(), memory);
	table->vkFreeMemory(device->get_device(), memory, nullptr);
	heap.size -= size;
}

void DeviceAllocator::garbage_collect()
{
	ALLOCATOR_LOCK();
	for (auto &heap : heaps)
		heap.garbage_collect(device);
}

void *DeviceAllocator::map_memory(const DeviceAllocation &alloc, MemoryAccessFlags flags,
                                  VkDeviceSize offset, VkDeviceSize length)
{
	VkDeviceSize base_offset = offset;

	// This will only happen if the memory type is device local only, which we cannot possibly map.
	if (!alloc.host_base)
		return nullptr;

	if ((flags & MEMORY_ACCESS_READ_BIT) &&
	    !(mem_props.memoryTypes[alloc.memory_type].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
	{
		offset += alloc.offset;
		VkDeviceSize end_offset = offset + length;
		offset &= ~(atom_alignment - 1);
		length = end_offset - offset;
		VkDeviceSize size = (length + atom_alignment - 1) & ~(atom_alignment - 1);

		// Have to invalidate cache here.
		const VkMappedMemoryRange range = {
			VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, nullptr, alloc.base, offset, size,
		};
		table->vkInvalidateMappedMemoryRanges(device->get_device(), 1, &range);
	}

	return alloc.host_base + base_offset;
}

void DeviceAllocator::unmap_memory(const DeviceAllocation &alloc, MemoryAccessFlags flags,
                                   VkDeviceSize offset, VkDeviceSize length)
{
	// This will only happen if the memory type is device local only, which we cannot possibly map.
	if (!alloc.host_base)
		return;

	if ((flags & MEMORY_ACCESS_WRITE_BIT) &&
	    !(mem_props.memoryTypes[alloc.memory_type].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
	{
		offset += alloc.offset;
		VkDeviceSize end_offset = offset + length;
		offset &= ~(atom_alignment - 1);
		length = end_offset - offset;
		VkDeviceSize size = (length + atom_alignment - 1) & ~(atom_alignment - 1);

		// Have to flush caches here.
		const VkMappedMemoryRange range = {
			VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, nullptr, alloc.base, offset, size,
		};
		table->vkFlushMappedMemoryRanges(device->get_device(), 1, &range);
	}
}

bool DeviceAllocator::allocate(uint32_t size, uint32_t memory_type, VkDeviceMemory *memory, uint8_t **host_memory,
                               VkImage dedicated_image)
{
	ALLOCATOR_LOCK();
	auto &heap = heaps[mem_props.memoryTypes[memory_type].heapIndex];

	// Naive searching is fine here as vkAllocate blocks are *huge* and we won't have many of them.
	auto itr = end(heap.blocks);
	if (dedicated_image == VK_NULL_HANDLE)
	{
		itr = find_if(begin(heap.blocks), end(heap.blocks),
		              [=](const Allocation &alloc) { return size == alloc.size && memory_type == alloc.type; });
	}

	bool host_visible = (mem_props.memoryTypes[memory_type].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;

	// Found previously used block.
	if (itr != end(heap.blocks))
	{
		*memory = itr->memory;
		*host_memory = itr->host_memory;
		heap.blocks.erase(itr);
		return true;
	}

	VkMemoryAllocateInfo info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, size, memory_type };
	VkMemoryDedicatedAllocateInfoKHR dedicated = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_KHR };
	if (dedicated_image != VK_NULL_HANDLE)
	{
		dedicated.image = dedicated_image;
		info.pNext = &dedicated;
	}

	VkDeviceMemory device_memory;
	VkResult res = table->vkAllocateMemory(device->get_device(), &info, nullptr, &device_memory);

	if (res == VK_SUCCESS)
	{
		heap.size += size;
		*memory = device_memory;

		if (host_visible)
		{
			if (table->vkMapMemory(device->get_device(), device_memory, 0, size, 0, reinterpret_cast<void **>(host_memory)) != VK_SUCCESS)
				return false;
		}

		return true;
	}
	else
	{
		// Look through our heap and see if there are blocks of other types we can free.
		auto block_itr = begin(heap.blocks);
		while (res != VK_SUCCESS && itr != end(heap.blocks))
		{
			if (block_itr->host_memory)
				table->vkUnmapMemory(device->get_device(), block_itr->memory);
			table->vkFreeMemory(device->get_device(), block_itr->memory, nullptr);
			heap.size -= block_itr->size;
			res = table->vkAllocateMemory(device->get_device(), &info, nullptr, &device_memory);
			++block_itr;
		}

		heap.blocks.erase(begin(heap.blocks), block_itr);

		if (res == VK_SUCCESS)
		{
			heap.size += size;
			*memory = device_memory;

			if (host_visible)
			{
				if (table->vkMapMemory(device->get_device(), device_memory, 0, size, 0, reinterpret_cast<void **>(host_memory)) !=
				    VK_SUCCESS)
				{
					table->vkFreeMemory(device->get_device(), device_memory, nullptr);
					return false;
				}
			}

			return true;
		}
		else
			return false;
	}
}
}
