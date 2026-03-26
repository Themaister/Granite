/* Copyright (c) 2017-2026 Hans-Kristian Arntzen
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
#include "timeline_trace_file.hpp"
#include "device.hpp"
#include <algorithm>

#ifndef _WIN32
#include <unistd.h>
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace Vulkan
{
static bool allocation_mode_supports_bda(AllocationMode mode)
{
	switch (mode)
	{
	case AllocationMode::LinearDevice:
	case AllocationMode::LinearHostMappable:
	case AllocationMode::LinearDeviceHighPriority:
		return true;

	default:
		break;
	}

	return false;
}

void DeviceAllocation::free_immediate()
{
	if (!alloc)
		return;

	alloc->free(heap, mask);
	alloc = nullptr;
	base = VK_NULL_HANDLE;
	mask = 0;
	offset = 0;
}

ExternalHandle DeviceAllocation::export_handle(Device &device)
{
	ExternalHandle h;

	if (exportable_types == 0)
	{
		LOGE("Cannot export from this allocation.\n");
		return h;
	}

	auto &table = device.get_device_table();

#ifdef _WIN32
	VkMemoryGetWin32HandleInfoKHR handle_info = { VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR };
	handle_info.handleType = static_cast<VkExternalMemoryHandleTypeFlagBits>(exportable_types);
	handle_info.memory = base;
	h.memory_handle_type = handle_info.handleType;

	if (table.vkGetMemoryWin32HandleKHR(device.get_device(), &handle_info, &h.handle) != VK_SUCCESS)
	{
		LOGE("Failed to export memory handle.\n");
		h.handle = nullptr;
	}
#else
	VkMemoryGetFdInfoKHR fd_info = { VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR };
	fd_info.handleType = static_cast<VkExternalMemoryHandleTypeFlagBits>(exportable_types);
	fd_info.memory = base;
	h.memory_handle_type = fd_info.handleType;

	if (table.vkGetMemoryFdKHR(device.get_device(), &fd_info, &h.handle) != VK_SUCCESS)
	{
		LOGE("Failed to export memory handle.\n");
		h.handle = -1;
	}
#endif

	return h;
}

void DeviceAllocation::free_immediate(DeviceAllocator &allocator)
{
	if (alloc)
		free_immediate();
	else if (base)
	{
		allocator.internal_free_no_recycle(size, memory_type, base);
		base = VK_NULL_HANDLE;
	}
}

void DeviceAllocation::free_global(DeviceAllocator &allocator, uint32_t size_, uint32_t memory_type_)
{
	if (base)
	{
		allocator.internal_free(size_, memory_type_, mode, base, host_base != nullptr);
		base = VK_NULL_HANDLE;
		mask = 0;
		offset = 0;
	}
}

void ClassAllocator::prepare_allocation(DeviceAllocation *alloc, Util::IntrusiveList<MiniHeap>::Iterator heap_itr,
                                        const Util::SuballocationResult &suballoc)
{
	auto &heap = *heap_itr;
	alloc->heap = heap_itr;
	alloc->base = heap.allocation.base;
	alloc->offset = suballoc.offset + heap.allocation.offset;
	alloc->mask = suballoc.mask;
	alloc->size = suballoc.size;

	if (heap.allocation.host_base)
		alloc->host_base = heap.allocation.host_base + suballoc.offset;

	VK_ASSERT(heap.allocation.mode == global_allocator_mode);
	VK_ASSERT(heap.allocation.memory_type == memory_type);

	alloc->mode = global_allocator_mode;
	alloc->memory_type = memory_type;
	alloc->alloc = this;
}

static inline bool mode_request_host_mapping(AllocationMode mode)
{
	// LinearHostMapping will always work. LinearDevice ones will speculatively work on UMA.
	return mode == AllocationMode::LinearHostMappable ||
	       mode == AllocationMode::LinearDevice ||
	       mode == AllocationMode::LinearDeviceHighPriority;
}

bool ClassAllocator::allocate_backing_heap(DeviceAllocation *alloc)
{
	uint32_t alloc_size = sub_block_size * Util::LegionAllocator::NumSubBlocks;

	if (parent)
	{
		return parent->allocate(alloc_size, alloc);
	}
	else
	{
		alloc->offset = 0;
		alloc->host_base = nullptr;
		alloc->mode = global_allocator_mode;
		alloc->memory_type = memory_type;

		return global_allocator->internal_allocate(
		    alloc_size, memory_type, global_allocator_mode, &alloc->base,
		    mode_request_host_mapping(global_allocator_mode) ? &alloc->host_base : nullptr,
		    VK_OBJECT_TYPE_DEVICE, 0, nullptr);
	}
}

void ClassAllocator::free_backing_heap(DeviceAllocation *allocation)
{
	assert(allocation->mode == global_allocator_mode);
	assert(allocation->memory_type == memory_type);

	// Our mini-heap is completely freed, free to higher level allocator.
	if (parent)
		allocation->free_immediate();
	else
		allocation->free_global(*global_allocator, sub_block_size * Util::LegionAllocator::NumSubBlocks, memory_type);
}

bool Allocator::allocate_global(uint32_t size, AllocationMode mode, DeviceAllocation *alloc)
{
	// Fall back to global allocation, do not recycle.
	alloc->host_base = nullptr;
	if (!global_allocator->internal_allocate(
		size, memory_type, mode, &alloc->base,
		mode_request_host_mapping(mode) ? &alloc->host_base : nullptr,
		VK_OBJECT_TYPE_DEVICE, 0, nullptr))
	{
		return false;
	}

	alloc->mode = mode;
	alloc->alloc = nullptr;
	alloc->memory_type = memory_type;
	alloc->size = size;
	return true;
}

bool Allocator::allocate_dedicated(uint32_t size, AllocationMode mode, DeviceAllocation *alloc,
                                   VkObjectType type, uint64_t object, ExternalHandle *external)
{
	// Fall back to global allocation, do not recycle.
	alloc->host_base = nullptr;
	if (!global_allocator->internal_allocate(
		size, memory_type, mode, &alloc->base,
		mode_request_host_mapping(mode) ? &alloc->host_base : nullptr,
		type, object, external))
	{
		return false;
	}

	alloc->mode = mode;
	alloc->alloc = nullptr;
	alloc->memory_type = memory_type;
	alloc->size = size;

	// If we imported memory instead, do not allow handle export.
	if (external && !(*external))
		alloc->exportable_types = external->memory_handle_type;

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

bool Allocator::allocate(uint32_t size, uint32_t alignment, AllocationMode mode, DeviceAllocation *alloc)
{
	for (auto &c : classes)
	{
		auto &suballocator = c[unsigned(mode)];

		// Find a suitable class to allocate from.
		if (size <= suballocator.get_max_allocation_size())
		{
			if (alignment > suballocator.get_block_alignment())
			{
				size_t padded_size = size + (alignment - suballocator.get_block_alignment());
				if (padded_size <= suballocator.get_max_allocation_size())
					size = padded_size;
				else
					continue;
			}

			bool ret = suballocator.allocate(size, alloc);
			if (ret)
			{
				uint32_t aligned_offset = (alloc->offset + alignment - 1) & ~(alignment - 1);
				if (alloc->host_base)
					alloc->host_base += aligned_offset - alloc->offset;
				alloc->offset = aligned_offset;
				VK_ASSERT(alloc->mode == mode);
				VK_ASSERT(alloc->memory_type == memory_type);
			}

			return ret;
		}
	}

	if (!allocate_global(size, mode, alloc))
		return false;

	VK_ASSERT(alloc->mode == mode);
	VK_ASSERT(alloc->memory_type == memory_type);
	return true;
}

Allocator::Allocator(Util::ObjectPool<MiniHeap> &object_pool)
{
	for (int i = 0; i < Util::ecast(MemoryClass::Count) - 1; i++)
		for (int j = 0; j < Util::ecast(AllocationMode::Count); j++)
			classes[i][j].set_parent(&classes[i + 1][j]);

	for (auto &c : classes)
		for (auto &m : c)
			m.set_object_pool(&object_pool);

	for (int j = 0; j < Util::ecast(AllocationMode::Count); j++)
	{
		auto mode = static_cast<AllocationMode>(j);

		// 128 chunk
		get_class_allocator(MemoryClass::Small, mode).set_sub_block_size(128);
		// 4k chunk
		get_class_allocator(MemoryClass::Medium, mode).set_sub_block_size(
			128 * Util::LegionAllocator::NumSubBlocks); // 4K
		// 128k chunk
		get_class_allocator(MemoryClass::Large, mode).set_sub_block_size(
			128 * Util::LegionAllocator::NumSubBlocks *
			Util::LegionAllocator::NumSubBlocks);
		// 2M chunk
		get_class_allocator(MemoryClass::Huge, mode).set_sub_block_size(
			64 * Util::LegionAllocator::NumSubBlocks * Util::LegionAllocator::NumSubBlocks *
			Util::LegionAllocator::NumSubBlocks);
	}
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
		allocators.emplace_back(new Allocator(object_pool));
		allocators.back()->set_global_allocator(this, i);
	}

	HeapBudget budgets[VK_MAX_MEMORY_HEAPS];
	get_memory_budget(budgets);

	// Figure out if we have a PCI-e BAR heap.
	// We need to be very careful with our budget (usually 128 MiB out of 256 MiB) on these heaps
	// since they can lead to instability if overused.
	VkMemoryPropertyFlags combined_allowed_flags[VK_MAX_MEMORY_HEAPS] = {};
	for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++)
	{
		uint32_t heap_index = mem_props.memoryTypes[i].heapIndex;
		combined_allowed_flags[heap_index] |= mem_props.memoryTypes[i].propertyFlags;
	}

	bool has_host_only_heap = false;
	bool has_device_only_heap = false;
	VkDeviceSize host_heap_size = 0;
	VkDeviceSize device_heap_size = 0;
	const VkMemoryPropertyFlags pinned_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
	                                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
	for (uint32_t i = 0; i < mem_props.memoryHeapCount; i++)
	{
		if ((combined_allowed_flags[i] & pinned_flags) == VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
		{
			has_host_only_heap = true;
			host_heap_size = (std::max)(host_heap_size, mem_props.memoryHeaps[i].size);
		}
		else if ((combined_allowed_flags[i] & pinned_flags) == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
		{
			has_device_only_heap = true;
			device_heap_size = (std::max)(device_heap_size, mem_props.memoryHeaps[i].size);
		}
	}

	// If we have ReBAR enabled, we generally won't find DEVICE only and HOST only heaps.
	// Budget criticalness should only be considered if we have the default small BAR heap (256 MiB).
	if (has_host_only_heap && has_device_only_heap)
	{
		for (uint32_t i = 0; i < mem_props.memoryHeapCount; i++)
		{
			if ((combined_allowed_flags[i] & pinned_flags) == pinned_flags &&
			    mem_props.memoryHeaps[i].size < host_heap_size &&
			    mem_props.memoryHeaps[i].size < device_heap_size)
			{
				memory_heap_is_budget_critical[i] = true;
			}
		}
	}
}

bool DeviceAllocator::allocate_generic_memory(uint32_t size, uint32_t alignment, AllocationMode mode,
                                              uint32_t memory_type, DeviceAllocation *alloc)
{
	return allocators[memory_type]->allocate(size, alignment, mode, alloc);
}

bool DeviceAllocator::allocate_buffer_memory(uint32_t size, uint32_t alignment, AllocationMode mode,
                                             uint32_t memory_type, VkBuffer buffer,
                                             DeviceAllocation *alloc, ExternalHandle *external)
{
	if (mode == AllocationMode::External)
	{
		return allocators[memory_type]->allocate_dedicated(
			size, mode, alloc,
			VK_OBJECT_TYPE_BUFFER, (uint64_t)buffer, external);
	}
	else
	{
		return allocate_generic_memory(size, alignment, mode, memory_type, alloc);
	}
}

bool DeviceAllocator::allocate_image_memory(uint32_t size, uint32_t alignment, AllocationMode mode, uint32_t memory_type,
                                            VkImage image, bool force_no_dedicated, DeviceAllocation *alloc,
                                            ExternalHandle *external)
{
	if (force_no_dedicated)
	{
		VK_ASSERT(mode != AllocationMode::External && !external);
		return allocate_generic_memory(size, alignment, mode, memory_type, alloc);
	}

	VkImageMemoryRequirementsInfo2 info = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2 };
	info.image = image;

	VkMemoryDedicatedRequirements dedicated_req = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS };
	VkMemoryRequirements2 mem_req = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2 };
	mem_req.pNext = &dedicated_req;
	table->vkGetImageMemoryRequirements2(device->get_device(), &info, &mem_req);

	if (dedicated_req.prefersDedicatedAllocation ||
	    dedicated_req.requiresDedicatedAllocation ||
	    mode == AllocationMode::External)
	{
		return allocators[memory_type]->allocate_dedicated(
			size, mode, alloc, VK_OBJECT_TYPE_IMAGE, (uint64_t)image, external);
	}
	else
	{
		return allocate_generic_memory(size, alignment, mode, memory_type, alloc);
	}
}

void DeviceAllocator::Heap::garbage_collect(Device *device_)
{
	auto &table_ = device_->get_device_table();
	for (auto &block : blocks)
	{
		table_.vkFreeMemory(device_->get_device(), block.memory, nullptr);
		size -= block.size;
	}
	blocks.clear();
}

DeviceAllocator::~DeviceAllocator()
{
	for (auto &heap : heaps)
		heap.garbage_collect(device);
}

void DeviceAllocator::internal_free(uint32_t size, uint32_t memory_type, AllocationMode mode, VkDeviceMemory memory, bool is_mapped)
{
	if (is_mapped)
		table->vkUnmapMemory(device->get_device(), memory);

	auto &heap = heaps[mem_props.memoryTypes[memory_type].heapIndex];

	VK_ASSERT(mode != AllocationMode::Count);

	heap.blocks.push_back({ memory, size, memory_type, mode });
	if (memory_heap_is_budget_critical[mem_props.memoryTypes[memory_type].heapIndex])
		heap.garbage_collect(device);
}

void DeviceAllocator::internal_free_no_recycle(uint32_t size, uint32_t memory_type, VkDeviceMemory memory)
{
	auto &heap = heaps[mem_props.memoryTypes[memory_type].heapIndex];
	table->vkFreeMemory(device->get_device(), memory, nullptr);
	heap.size -= size;
}

void DeviceAllocator::garbage_collect()
{
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

void DeviceAllocator::get_memory_budget_nolock(HeapBudget *heap_budgets)
{
	uint32_t num_heaps = mem_props.memoryHeapCount;

	if (device->get_device_features().supports_memory_budget)
	{
		VkPhysicalDeviceMemoryProperties2 props =
				{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2 };
		VkPhysicalDeviceMemoryBudgetPropertiesEXT budget_props =
				{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT };

		if (device->get_device_features().supports_memory_budget)
			props.pNext = &budget_props;

		vkGetPhysicalDeviceMemoryProperties2(device->get_physical_device(), &props);

		for (uint32_t i = 0; i < num_heaps; i++)
		{
			auto &heap = heap_budgets[i];
			heap.max_size = mem_props.memoryHeaps[i].size;
			heap.budget_size = budget_props.heapBudget[i];
			heap.device_usage = budget_props.heapUsage[i];
			heap.tracked_usage = heaps[i].size;
		}
	}
	else
	{
		for (uint32_t i = 0; i < num_heaps; i++)
		{
			auto &heap = heap_budgets[i];
			heap.max_size = mem_props.memoryHeaps[i].size;
			// Allow 75%.
			heap.budget_size = heap.max_size - (heap.max_size / 4);
			heap.tracked_usage = heaps[i].size;
			heap.device_usage = heaps[i].size;
		}
	}
}

void DeviceAllocator::get_memory_budget(HeapBudget *heap_budgets)
{
	get_memory_budget_nolock(heap_budgets);
}

bool DeviceAllocator::internal_allocate(
	uint32_t size, uint32_t memory_type, AllocationMode mode,
	VkDeviceMemory *memory, uint8_t **host_memory,
	VkObjectType object_type, uint64_t dedicated_object, ExternalHandle *external)
{
	uint32_t heap_index = mem_props.memoryTypes[memory_type].heapIndex;
	auto &heap = heaps[heap_index];

	// Naive searching is fine here as vkAllocate blocks are *huge* and we won't have many of them.
	auto itr = end(heap.blocks);
	if (dedicated_object == 0 && !external)
	{
		itr = find_if(begin(heap.blocks), end(heap.blocks),
		              [=](const Allocation &alloc) { return size == alloc.size && memory_type == alloc.type && mode == alloc.mode; });
	}

	bool host_visible = (mem_props.memoryTypes[memory_type].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0 &&
	                    host_memory != nullptr;

	// Found previously used block.
	if (itr != end(heap.blocks))
	{
		*memory = itr->memory;
		if (host_visible)
		{
			if (table->vkMapMemory(device->get_device(), itr->memory, 0, VK_WHOLE_SIZE,
			                       0, reinterpret_cast<void **>(host_memory)) != VK_SUCCESS)
				return false;
		}
		heap.blocks.erase(itr);
		return true;
	}

	// Don't bother checking against budgets on external memory.
	// It's not very meaningful.
	if (!external)
	{
		HeapBudget budgets[VK_MAX_MEMORY_HEAPS];
		get_memory_budget_nolock(budgets);

#ifdef VULKAN_DEBUG
		LOGI("Allocating %.1f MiB on heap #%u (mode #%u), before allocating budget: (%.1f MiB / %.1f MiB) [%.1f / %.1f].\n",
		     double(size) / double(1024 * 1024), heap_index, unsigned(mode),
		     double(budgets[heap_index].device_usage) / double(1024 * 1024),
		     double(budgets[heap_index].budget_size) / double(1024 * 1024),
		     double(budgets[heap_index].tracked_usage) / double(1024 * 1024),
		     double(budgets[heap_index].max_size) / double(1024 * 1024));
#endif

		const auto log_heap_index = [&]()
		{
			LOGW("  Size: %u MiB.\n", unsigned(size / (1024 * 1024)));
			LOGW("  Device usage: %u MiB.\n", unsigned(budgets[heap_index].device_usage / (1024 * 1024)));
			LOGW("  Tracked usage: %u MiB.\n", unsigned(budgets[heap_index].tracked_usage / (1024 * 1024)));
			LOGW("  Budget size: %u MiB.\n", unsigned(budgets[heap_index].budget_size / (1024 * 1024)));
			LOGW("  Max size: %u MiB.\n", unsigned(budgets[heap_index].max_size / (1024 * 1024)));
		};

		// If we're going to blow out the budget, we should recycle a bit.
		if (budgets[heap_index].device_usage + size >= budgets[heap_index].budget_size)
		{
			LOGW("Will exceed memory budget, cleaning up ...\n");
			log_heap_index();
			heap.garbage_collect(device);
		}

		get_memory_budget_nolock(budgets);
		if (budgets[heap_index].device_usage + size >= budgets[heap_index].budget_size)
		{
			LOGW("Even after garbage collection, we will exceed budget ...\n");
			if (memory_heap_is_budget_critical[heap_index])
				return false;
			log_heap_index();
		}
	}

	VkMemoryAllocateInfo info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, size, memory_type };
	VkMemoryDedicatedAllocateInfo dedicated = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO };
	VkExportMemoryAllocateInfo export_info = { VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO };
	VkMemoryPriorityAllocateInfoEXT priority_info = { VK_STRUCTURE_TYPE_MEMORY_PRIORITY_ALLOCATE_INFO_EXT };
	VkMemoryAllocateFlagsInfo flags_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO };
#ifdef _WIN32
	VkImportMemoryWin32HandleInfoKHR import_info = { VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR };
#else
	VkImportMemoryFdInfoKHR import_info = { VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR };
#endif

	if (dedicated_object != 0)
	{
		if (object_type == VK_OBJECT_TYPE_IMAGE)
			dedicated.image = (VkImage)dedicated_object;
		else if (object_type == VK_OBJECT_TYPE_BUFFER)
			dedicated.buffer = (VkBuffer)dedicated_object;
		info.pNext = &dedicated;
	}

	if (external)
	{
		VK_ASSERT(dedicated_object);

		if (bool(*external))
		{
			import_info.handleType = external->memory_handle_type;
			import_info.pNext = info.pNext;
			info.pNext = &import_info;

#ifdef _WIN32
			import_info.handle = external->handle;
#else
			import_info.fd = external->handle;
#endif
		}
		else
		{
			export_info.handleTypes = external->memory_handle_type;
			export_info.pNext = info.pNext;
			info.pNext = &export_info;
		}
	}

	// Don't bother with memory priority on external objects.
	if (device->get_device_features().memory_priority_features.memoryPriority && !external)
	{
		switch (mode)
		{
		case AllocationMode::LinearDeviceHighPriority:
		case AllocationMode::OptimalRenderTarget:
			priority_info.priority = 1.0f;
			break;

		case AllocationMode::LinearDevice:
		case AllocationMode::OptimalResource:
			priority_info.priority = 0.5f;
			break;

		default:
			priority_info.priority = 0.0f;
			break;
		}

		priority_info.pNext = info.pNext;
		info.pNext = &priority_info;
	}

	if (device->get_device_features().vk12_features.bufferDeviceAddress &&
	    allocation_mode_supports_bda(mode))
	{
		flags_info.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
		flags_info.pNext = info.pNext;
		info.pNext = &flags_info;
	}

	VkDeviceMemory device_memory;
	VkResult res;
	{
		GRANITE_SCOPED_TIMELINE_EVENT_FILE(device->get_system_handles().timeline_trace_file, "vkAllocateMemory");
		res = table->vkAllocateMemory(device->get_device(), &info, nullptr, &device_memory);
	}

	// If we're importing, make sure we consume the native handle.
	if (external && bool(*external) &&
	    ExternalHandle::memory_handle_type_imports_by_reference(external->memory_handle_type))
	{
#ifdef _WIN32
		::CloseHandle(external->handle);
#else
		::close(external->handle);
#endif
	}

	if (res == VK_SUCCESS)
	{
		heap.size += size;
		*memory = device_memory;

		if (host_visible)
		{
			if (table->vkMapMemory(device->get_device(), device_memory, 0, VK_WHOLE_SIZE,
			                       0, reinterpret_cast<void **>(host_memory)) != VK_SUCCESS)
			{
				table->vkFreeMemory(device->get_device(), device_memory, nullptr);
				heap.size -= size;
				return false;
			}
		}

		return true;
	}
	else
	{
		// Look through our heap and see if there are blocks of other types we can free.
		auto block_itr = begin(heap.blocks);
		while (res != VK_SUCCESS && itr != end(heap.blocks))
		{
			table->vkFreeMemory(device->get_device(), block_itr->memory, nullptr);
			heap.size -= block_itr->size;
			{
				GRANITE_SCOPED_TIMELINE_EVENT_FILE(device->get_system_handles().timeline_trace_file,
				                                   "vkAllocateMemory");
				res = table->vkAllocateMemory(device->get_device(), &info, nullptr, &device_memory);
			}
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
					heap.size -= size;
					return false;
				}
			}

			return true;
		}
		else
			return false;
	}
}

DeviceAllocationOwner::DeviceAllocationOwner(Device *device_, const DeviceAllocation &alloc_)
	: device(device_), alloc(alloc_)
{
}

DeviceAllocationOwner::~DeviceAllocationOwner()
{
	if (alloc.get_memory())
		device->free_memory(alloc);
}

const DeviceAllocation &DeviceAllocationOwner::get_allocation() const
{
	return alloc;
}

void DeviceAllocationDeleter::operator()(DeviceAllocationOwner *owner)
{
	owner->device->handle_pool.allocations.free(owner);
}

static VkDeviceSize align(VkDeviceSize value, uint32_t alignment)
{
	return (value + alignment - 1) & ~VkDeviceSize(alignment - 1);
}

bool DescriptorBufferAllocator::init(Vulkan::Device *device_)
{
	device = device_;

	if (!device->get_device_features().supports_descriptor_buffer_or_heap)
		return true;

	alignment = device->get_device_features().resource_heap_offset_alignment;
	sub_block_size = std::max<uint32_t>(device->get_gpu_properties().limits.nonCoherentAtomSize, alignment);

	VkDeviceSize max_range, max_descriptor_size;
	auto &heap_props = device->get_device_features().descriptor_heap_properties;
	auto heap = device->get_device_features().descriptor_heap_features.descriptorHeap;

	if (heap)
	{
		max_range = device->get_device_features().descriptor_heap_properties.maxResourceHeapSize;

		auto image_size = align(heap_props.imageDescriptorSize, heap_props.imageDescriptorAlignment);
		auto buffer_size = align(heap_props.bufferDescriptorSize, heap_props.bufferDescriptorAlignment);

		max_descriptor_size = std::max<uint32_t>(image_size, buffer_size);

		// We may use combinedImageSampler mode which is 20-bit index.
		max_range = std::min<VkDeviceSize>(max_range, image_size * 1024 * 1024);
	}
	else
	{
		max_range = std::min<VkDeviceSize>(
				device->get_device_features().descriptor_buffer_properties.maxResourceDescriptorBufferRange,
				device->get_device_features().descriptor_buffer_properties.maxSamplerDescriptorBufferRange);

		max_descriptor_size = std::max<uint32_t>(
				device->get_device_features().descriptor_buffer_properties.sampledImageDescriptorSize,
				device->get_device_features().descriptor_buffer_properties.robustStorageBufferDescriptorSize);
	}

	// Allocate this early so we're guaranteed to fit in smol BAR as well.
	BufferCreateInfo info = {};

	// Aim for a global heap of about 1M descriptors. Should be enough to avoid exhaustion.
	max_range = std::min<VkDeviceSize>(max_range, 1024ull * 1024ull * max_descriptor_size);

	auto max_sub_blocks = max_range / sub_block_size;
	auto max_sub_blocks_log2 = Util::floor_log2(max_sub_blocks);
	info.size = VkDeviceSize(sub_block_size) << max_sub_blocks_log2;
	Util::SliceAllocator::init(sub_block_size, max_sub_blocks_log2, &backing_va);

	if (heap)
	{
		// Only allow dynamic allocation from lower half of the heap. Upper range is slab allocated.
		max_sub_blocks /= 2;
		max_sub_blocks_log2--;
	}

	info.domain = BufferDomain::LinkedDeviceHost;

	if (heap)
	{
		info.usage = VK_BUFFER_USAGE_DESCRIPTOR_HEAP_BIT_EXT;
	}
	else
	{
		info.usage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT |
					 VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT;
	}

	auto buf = device->create_buffer(info);
	if (!buf)
	{
		LOGE("Failed to allocate descriptor buffer.\n");
		return false;
	}
	device->set_name(*buf, "resource-heap");

	init_copy_func(sampled_image_copy, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
	init_copy_func(storage_image_copy, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
	init_copy_func(input_attachment_copy, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT);
	init_copy_func(combined_image_copy, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	init_copy_func(sampler_copy, VK_DESCRIPTOR_TYPE_SAMPLER);
	init_copy_func(uniform_texel_copy, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER);
	init_copy_func(storage_texel_copy, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER);
	init_copy_func(ubo_copy, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
	init_copy_func(ssbo_copy, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

	resource_buffer = buf.release();

	resource_heap.size = info.size;
	resource_heap.mapped = static_cast<uint8_t *>(device->map_host_buffer(*resource_buffer, MEMORY_ACCESS_WRITE_BIT));
	resource_heap.va = resource_buffer->get_device_address();

	if (heap)
	{
		resource_heap.reserved_offset = info.size - heap_props.minResourceHeapReservedRange;
		// Ensure reserved offset is valid.
		resource_heap.reserved_offset &= ~VkDeviceSize(alignment - 1);

		// Split the resource heap in two.
		// Lower half is POT sized and allows for dynamic allocation.
		// This is used to spill out UBOs and SSBOs which must live as descriptors.
		// Also used to allocate bindless images for GPU perf.
		auto heap_dynamic_allocator_size = VkDeviceSize(sub_block_size) << max_sub_blocks_log2;
		auto num_application_resources =
			(resource_heap.reserved_offset - heap_dynamic_allocator_size) >> device->get_device_features().resource_heap_resource_desc_size_log2;
		auto resource_slab_offset = heap_dynamic_allocator_size >> device->get_device_features().resource_heap_resource_desc_size_log2;

		heap_resource_indices.reserve(num_application_resources);
		for (uint32_t i = num_application_resources; i; i--)
			heap_resource_indices.push_back(resource_slab_offset + i - 1);

		// Allocate the sampler heap. We only slab allocate out of this since it's so small.
		auto sampler_size = align(heap_props.samplerDescriptorSize, heap_props.samplerDescriptorAlignment);
		info.size = std::min<VkDeviceSize>(heap_props.maxSamplerHeapSize, 4096 * sampler_size);

		buf = device->create_buffer(info);
		if (!buf)
		{
			LOGE("Failed to allocate sampler heap.\n");
			return false;
		}

		device->set_name(*buf, "sampler-heap");
		sampler_buffer = buf.release();

		sampler_heap.size = info.size;
		sampler_heap.reserved_offset = info.size - heap_props.minSamplerHeapReservedRange;
		sampler_heap.mapped = static_cast<uint8_t *>(device->map_host_buffer(*sampler_buffer, MEMORY_ACCESS_WRITE_BIT));
		sampler_heap.va = sampler_buffer->get_device_address();

		sampler_heap.reserved_offset &= ~VkDeviceSize(heap_props.samplerDescriptorAlignment - 1);
		auto num_application_samplers = sampler_heap.reserved_offset / sampler_size;
		heap_sampler_indices.reserve(num_application_samplers);
		for (uint32_t i = num_application_samplers; i; i--)
			heap_sampler_indices.push_back(i - 1);
	}

	return true;
}

template <size_t N>
static void static_memcpy(uint8_t *dst, const uint8_t *src, size_t)
{
	// memcpy with static size is way more efficient than dynamic size.
	memcpy(dst, src, N);
}

static void dynamic_memcpy(uint8_t *dst, const uint8_t *src, size_t n)
{
	memcpy(dst, src, n);
}

template <size_t N>
static void static_memcpy_n(uint8_t *dst, const uint8_t * const *src, size_t count, size_t)
{
	// memcpy with static size is way more efficient than dynamic size.
	for (size_t i = 0; i < count; i++, dst += N)
		memcpy(dst, src[i], N);
}

static void dynamic_memcpy_n(uint8_t *dst, const uint8_t * const *src, size_t count, size_t n)
{
	for (size_t i = 0; i < count; i++, dst += n)
		memcpy(dst, src[i], n);
}

static DescriptorCopyFunc get_optimized_copy_func(size_t size)
{
	switch (size)
	{
	case 0: return static_memcpy<0>;
	case 4: return static_memcpy<4>;
	case 8: return static_memcpy<8>;
	case 16: return static_memcpy<16>;
	case 32: return static_memcpy<32>;
	case 48: return static_memcpy<48>;
	case 64: return static_memcpy<64>;
	case 96: return static_memcpy<96>;
	case 128: return static_memcpy<128>;
	case 192: return static_memcpy<192>;
	case 256: return static_memcpy<256>;
	default: LOGW("Unrecognized special memcpy size %zu. Using slow fallback.\n", size); return dynamic_memcpy;
	}
}

static DescriptorCopyNFunc get_optimized_copy_n_func(size_t size)
{
	switch (size)
	{
	case 0: return static_memcpy_n<0>;
	case 4: return static_memcpy_n<4>;
	case 8: return static_memcpy_n<8>;
	case 16: return static_memcpy_n<16>;
	case 32: return static_memcpy_n<32>;
	case 48: return static_memcpy_n<48>;
	case 64: return static_memcpy_n<64>;
	case 96: return static_memcpy_n<96>;
	case 128: return static_memcpy_n<128>;
	case 192: return static_memcpy_n<192>;
	case 256: return static_memcpy_n<256>;
	default: LOGW("Unrecognized special memcpy size %zu. Using slow fallback.\n", size); return dynamic_memcpy_n;
	}
}

void DescriptorBufferAllocator::init_copy_func(DescriptorTypeInfo &info, VkDescriptorType type) const
{
	info.size = get_descriptor_size_for_type(type);
	info.func = get_optimized_copy_func(info.size);
	info.func_n = get_optimized_copy_n_func(info.size);
	info.slab.init(info.size);
}

void DescriptorBufferAllocator::free(const DescriptorBufferAllocation &alloc)
{
	std::lock_guard<std::mutex> holder{lock};
	Util::SliceAllocator::free(alloc.backing_slice);
}

void DescriptorBufferAllocator::free(const DescriptorBufferAllocation *alloc, size_t count)
{
	std::lock_guard<std::mutex> holder{lock};
	for (size_t i = 0; i < count; i++)
	{
		total_size -= alloc[i].backing_slice.count;
		Util::SliceAllocator::free(alloc[i].backing_slice);
	}
}

DescriptorBufferAllocation DescriptorBufferAllocator::allocate(VkDeviceSize size)
{
	size = (size + alignment - 1) & ~(alignment - 1);

	std::lock_guard<std::mutex> holder{lock};

	DescriptorBufferAllocation alloc = {};
	if (!Util::SliceAllocator::allocate(size, &alloc.backing_slice))
	{
		LOGE("Descriptor buffer arena is exhausted! This should not happen.\n");
		return alloc;
	}

	total_size += alloc.backing_slice.count;
	if (total_size > high_water_mark)
	{
		high_water_mark = total_size;
#ifdef VULKAN_DEBUG
		LOGI("Descriptor arena high water mark increased to: %llu bytes.\n",
		     static_cast<unsigned long long>(high_water_mark));
#endif
	}

	return alloc;
}

void DescriptorBufferAllocator::teardown()
{
	if (resource_buffer)
	{
		resource_buffer->set_internal_sync_object();
		resource_buffer->release_reference();
		resource_buffer = nullptr;
	}

	if (sampler_buffer)
	{
		sampler_buffer->set_internal_sync_object();
		sampler_buffer->release_reference();
		sampler_buffer = nullptr;
	}
}

VkSampler DescriptorBufferAllocator::create_sampler(const VkSamplerCreateInfo *info)
{
	if (device->get_device_features().descriptor_heap_features.descriptorHeap)
	{
		uint32_t index;
		{
			std::lock_guard<std::mutex> holder{lock};
			if (heap_sampler_indices.empty())
				return VK_NULL_HANDLE;

			index = heap_sampler_indices.back();
			heap_sampler_indices.pop_back();
		}

		auto &props = device->get_device_features().descriptor_heap_properties;
		uint8_t *mapped = sampler_heap.mapped + index * align(props.samplerDescriptorSize, props.samplerDescriptorAlignment);

		VkHostAddressRangeEXT addr = {};
		addr.address = mapped;
		addr.size = props.samplerDescriptorSize;
		device->get_device_table().vkWriteSamplerDescriptorsEXT(device->get_device(), 1, info, &addr);

		return (VkSampler)(uint64_t(index) | (1ull << 63));
	}
	else
	{
		VkSampler samp = VK_NULL_HANDLE;
		if (device->get_device_table().vkCreateSampler(device->get_device(), info, nullptr, &samp) != VK_SUCCESS)
			return VK_NULL_HANDLE;
		return samp;
	}
}

void DescriptorBufferAllocator::destroy_sampler(VkSampler sampler)
{
	if (device->get_device_features().descriptor_heap_features.descriptorHeap)
	{
		if (sampler)
		{
			VK_ASSERT(((uint64_t)sampler) >> 63);
			std::lock_guard<std::mutex> holder{lock};
			heap_sampler_indices.push_back((uint64_t)sampler);
		}
	}
	else
	{
		device->get_device_table().vkDestroySampler(device->get_device(), sampler, nullptr);
	}
}

uint32_t DescriptorBufferAllocator::allocate_single_resource_heap_entry()
{
	std::lock_guard<std::mutex> holder{lock};
	if (heap_resource_indices.empty())
	{
		LOGE("Resource heap is empty.\n");
		return UINT32_MAX;
	}

	auto ret = heap_resource_indices.back();
	heap_resource_indices.pop_back();
	return ret;
}

void DescriptorBufferAllocator::free_single_resource_heap_entry(uint32_t index)
{
	std::lock_guard<std::mutex> holder{lock};
	heap_resource_indices.push_back(index);
}

DescriptorBufferAllocator::~DescriptorBufferAllocator()
{
	// Call teardown before destroying device.
	VK_ASSERT(!resource_buffer);
	VK_ASSERT(!sampler_buffer);
	VK_ASSERT(total_size == 0);
}

uint32_t DescriptorBufferAllocator::get_descriptor_size_for_type(VkDescriptorType type) const
{
	auto &ext = device->get_device_features();

	if (ext.descriptor_heap_features.descriptorHeap)
	{
		// We could query the types individually but lots of other code relies
		// on these being normalized around a common value.

		switch (type)
		{
		case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
			// Is never used directly.
			return 0;

		case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
		case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
		case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
			return align(ext.descriptor_heap_properties.bufferDescriptorSize,
			             ext.descriptor_heap_properties.bufferDescriptorAlignment);

		default:
			return align(ext.descriptor_heap_properties.imageDescriptorSize,
			             ext.descriptor_heap_properties.imageDescriptorAlignment);
		}
	}
	else
	{
		switch (type)
		{
		case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
			return ext.descriptor_buffer_properties.combinedImageSamplerDescriptorSize;
		case VK_DESCRIPTOR_TYPE_SAMPLER:
			return ext.descriptor_buffer_properties.samplerDescriptorSize;
		case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
			return ext.descriptor_buffer_properties.sampledImageDescriptorSize;
		case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
			return ext.descriptor_buffer_properties.inputAttachmentDescriptorSize;
		case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
			return ext.descriptor_buffer_properties.storageImageDescriptorSize;
		case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
			if (ext.enabled_features.robustBufferAccess)
				return ext.descriptor_buffer_properties.robustUniformBufferDescriptorSize;
			else
				return ext.descriptor_buffer_properties.uniformBufferDescriptorSize;
		case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
			if (ext.enabled_features.robustBufferAccess)
				return ext.descriptor_buffer_properties.robustUniformTexelBufferDescriptorSize;
			else
				return ext.descriptor_buffer_properties.uniformTexelBufferDescriptorSize;
		case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
			if (ext.enabled_features.robustBufferAccess)
				return ext.descriptor_buffer_properties.robustStorageBufferDescriptorSize;
			else
				return ext.descriptor_buffer_properties.storageBufferDescriptorSize;
		case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
			if (ext.enabled_features.robustBufferAccess)
				return ext.descriptor_buffer_properties.robustStorageTexelBufferDescriptorSize;
			else
				return ext.descriptor_buffer_properties.storageTexelBufferDescriptorSize;
		case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
			return ext.descriptor_buffer_properties.accelerationStructureDescriptorSize;
		default:
			LOGE("Invalid descriptor type %u\n", type);
			return 0;
		}
	}
}

void DescriptorBufferAllocator::free_cached_descriptors(const CachedDescriptorPayload *payloads, size_t count)
{
	bool heap = device->get_device_features().descriptor_heap_features.descriptorHeap == VK_TRUE;

	for (size_t i = 0; i < count; i++)
	{
		if (!payloads[i].ptr)
			continue;

		switch (payloads[i].type)
		{
		case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
			combined_image_copy.slab.free(payloads[i].ptr);
			break;
		case VK_DESCRIPTOR_TYPE_SAMPLER:
			sampler_copy.slab.free(payloads[i].ptr);
			break;
		case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
			sampled_image_copy.slab.free(payloads[i].ptr);
			break;
		case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
			input_attachment_copy.slab.free(payloads[i].ptr);
			break;
		case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
			storage_image_copy.slab.free(payloads[i].ptr);
			break;
		case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
			ubo_copy.slab.free(payloads[i].ptr);
			break;
		case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
			uniform_texel_copy.slab.free(payloads[i].ptr);
			break;
		case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
			ssbo_copy.slab.free(payloads[i].ptr);
			break;
		case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
			storage_texel_copy.slab.free(payloads[i].ptr);
			break;
		default:
			break;
		}

		if (heap)
			free_single_resource_heap_entry(payloads[i].heap_index);
	}
}

bool DescriptorBufferAllocator::create_image_view(const VkImageViewCreateInfo &info, VkImageUsageFlags usage,
                                                  ImageLayout layout, CachedImageView &view)
{
	view = {};
	bool heap = device->get_device_features().descriptor_heap_features.descriptorHeap == VK_TRUE;

	static constexpr VkImageUsageFlags force_view_flags =
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
			VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT |
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
			VK_IMAGE_USAGE_VIDEO_ENCODE_DPB_BIT_KHR |
			VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR |
			VK_IMAGE_USAGE_VIDEO_ENCODE_DST_BIT_KHR |
			VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR |
			VK_IMAGE_USAGE_VIDEO_DECODE_SRC_BIT_KHR |
			VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR;

	VkImageViewUsageCreateInfo view_usage_create_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO };
	auto tmpinfo = info;
	view_usage_create_info.usage = usage;
	view_usage_create_info.pNext = tmpinfo.pNext;
	tmpinfo.pNext = &view_usage_create_info;
	if (heap)
		view_usage_create_info.usage &= force_view_flags;

	bool need_image_view_object = !heap || (usage & force_view_flags) != 0;
	auto &table = device->get_device_table();

	if (need_image_view_object &&
	    table.vkCreateImageView(device->get_device(), &tmpinfo, nullptr, &view.view) != VK_SUCCESS)
		return false;

	if (heap)
	{
		VkResourceDescriptorInfoEXT infos[4];
		VkImageDescriptorInfoEXT images[4];
		VkHostAddressRangeEXT addrs[4];
		uint32_t count = 0;

		if (usage & VK_IMAGE_USAGE_SAMPLED_BIT)
		{
			view.sampled = alloc_sampled_image();
			view.sampled.heap_index = allocate_single_resource_heap_entry();
			if (view.sampled.heap_index == UINT32_MAX)
				return false;

			infos[count] = { VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT };
			infos[count].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
			infos[count].data.pImage = &images[count];

			images[count] = { VK_STRUCTURE_TYPE_IMAGE_DESCRIPTOR_INFO_EXT };
			images[count].pView = &info;
			images[count].layout = layout == ImageLayout::Optimal ? VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;

			addrs[count].address = view.sampled.ptr;
			addrs[count].size = get_descriptor_size_for_type(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
			count++;
		}

		if (usage & VK_IMAGE_USAGE_STORAGE_BIT)
		{
			view.storage = alloc_storage_image();
			view.storage.heap_index = allocate_single_resource_heap_entry();
			if (view.storage.heap_index == UINT32_MAX)
				return false;

			infos[count] = { VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT };
			infos[count].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
			infos[count].data.pImage = &images[count];

			images[count] = { VK_STRUCTURE_TYPE_IMAGE_DESCRIPTOR_INFO_EXT };
			images[count].pView = &info;
			images[count].layout = VK_IMAGE_LAYOUT_GENERAL;

			addrs[count].address = view.storage.ptr;
			addrs[count].size = get_descriptor_size_for_type(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
			count++;
		}

		if (usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)
		{
			view.input_attachment = alloc_input_attachment();
			view.input_attachment.heap_index = allocate_single_resource_heap_entry();
			if (view.input_attachment.heap_index == UINT32_MAX)
				return false;

			view.input_attachment_feedback = alloc_input_attachment();
			view.input_attachment_feedback.heap_index = allocate_single_resource_heap_entry();
			if (view.input_attachment_feedback.heap_index == UINT32_MAX)
				return false;

			for (int i = 0; i < 2; i++)
			{
				infos[count] = { VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT };
				infos[count].type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
				infos[count].data.pImage = &images[count];

				images[count] = { VK_STRUCTURE_TYPE_IMAGE_DESCRIPTOR_INFO_EXT };
				images[count].pView = &info;
				images[count].layout = i == 0 && layout == ImageLayout::Optimal
					                       ? VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL
					                       : VK_IMAGE_LAYOUT_GENERAL;

				addrs[count].address = i ? view.input_attachment_feedback.ptr : view.input_attachment.ptr;
				addrs[count].size = get_descriptor_size_for_type(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT);
				count++;
			}
		}

		if (count)
			table.vkWriteResourceDescriptorsEXT(device->get_device(), count, infos, addrs);

		auto desc_size = device->get_device_features().resource_heap_resource_desc_size;

		if (usage & VK_IMAGE_USAGE_SAMPLED_BIT)
			copy_sampled_image(resource_heap.mapped + view.sampled.heap_index * desc_size, view.sampled.ptr);

		if (usage & VK_IMAGE_USAGE_STORAGE_BIT)
			copy_storage_image(resource_heap.mapped + view.storage.heap_index * desc_size, view.storage.ptr);

		if (usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)
		{
			copy_storage_image(resource_heap.mapped + view.input_attachment.heap_index * desc_size, view.input_attachment.ptr);
			copy_storage_image(resource_heap.mapped + view.input_attachment_feedback.heap_index * desc_size, view.input_attachment_feedback.ptr);
		}
	}
	else if (device->get_device_features().descriptor_buffer_features.descriptorBuffer &&
	         need_image_view_object)
	{
		VkDescriptorImageInfo image_info = {};
		image_info.imageView = view.view;

		VkDescriptorGetInfoEXT get_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT };
		get_info.data.pSampledImage = &image_info;

		auto &props = device->get_device_features().descriptor_buffer_properties;

		if (usage & VK_IMAGE_USAGE_SAMPLED_BIT)
		{
			get_info.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
			image_info.imageLayout = layout == ImageLayout::Optimal ? VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;
			view.sampled = alloc_sampled_image();
			table.vkGetDescriptorEXT(device->get_device(), &get_info, props.sampledImageDescriptorSize, view.sampled.ptr);
		}

		if (usage & VK_IMAGE_USAGE_STORAGE_BIT)
		{
			get_info.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
			image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
			view.storage = alloc_storage_image();
			table.vkGetDescriptorEXT(device->get_device(), &get_info, props.storageImageDescriptorSize, view.storage.ptr);
		}

		if (usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)
		{
			get_info.type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;

			image_info.imageLayout = layout == ImageLayout::Optimal ? VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;
			view.input_attachment = alloc_input_attachment();
			table.vkGetDescriptorEXT(device->get_device(), &get_info,
			                         props.inputAttachmentDescriptorSize, view.input_attachment.ptr);

			image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
			view.input_attachment_feedback = alloc_input_attachment();
			table.vkGetDescriptorEXT(device->get_device(), &get_info,
			                         props.inputAttachmentDescriptorSize, view.input_attachment_feedback.ptr);
		}
	}

	return true;
}

void DescriptorBufferAllocator::free_image_view(const CachedImageView &view)
{
	if (view.view)
		device->get_device_table().vkDestroyImageView(device->get_device(), view.view, nullptr);

	free_cached_descriptors(&view.sampled, 1);
	free_cached_descriptors(&view.storage, 1);
	free_cached_descriptors(&view.input_attachment, 1);
	free_cached_descriptors(&view.input_attachment_feedback, 1);
}

bool DescriptorBufferAllocator::create_buffer_view(
	const BufferViewCreateInfo &info, CachedBufferView &view)
{
	bool heap = device->get_device_features().descriptor_heap_features.descriptorHeap == VK_TRUE;
	auto &table = device->get_device_table();
	view = {};

	if (!device->get_device_features().supports_descriptor_buffer_or_heap)
	{
		VkBufferViewCreateInfo vk_info = { VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO };
		vk_info.buffer = info.buffer->get_buffer();
		vk_info.format = info.format;
		vk_info.offset = info.offset;
		vk_info.range = info.range;

		if (table.vkCreateBufferView(device->get_device(), &vk_info, nullptr, &view.view) != VK_SUCCESS)
			return false;
	}
	else if (heap)
	{
		VkTexelBufferDescriptorInfoEXT texel = { VK_STRUCTURE_TYPE_TEXEL_BUFFER_DESCRIPTOR_INFO_EXT };
		VkResourceDescriptorInfoEXT infos[2];
		VkHostAddressRangeEXT addrs[2];
		uint32_t count = 0;

		VkFormatProperties3 props3 = { VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3 };
		device->get_format_properties(info.format, &props3);

		texel.addressRange.address = info.buffer->get_device_address() + info.offset;
		if (info.range == VK_WHOLE_SIZE)
			texel.addressRange.size = info.buffer->get_create_info().size - info.offset;
		else
			texel.addressRange.size = info.range;
		texel.format = info.format;

		bool uniform =
				(info.buffer->get_create_info().usage & VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT) != 0 &&
				(props3.bufferFeatures & VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT) != 0;

		bool storage =
				(info.buffer->get_create_info().usage & VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT) != 0 &&
				(props3.bufferFeatures & VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT) != 0;

		if (uniform)
		{
			view.uniform = alloc_uniform_texel();
			view.uniform.heap_index = allocate_single_resource_heap_entry();

			infos[count] = { VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT };
			infos[count].type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
			infos[count].data.pTexelBuffer = &texel;
			addrs[count].address = view.uniform.ptr;
			addrs[count].size = get_descriptor_size_for_type(VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER);
			count++;
		}

		if (storage)
		{
			view.storage = alloc_storage_texel();
			view.storage.heap_index = allocate_single_resource_heap_entry();

			infos[count] = { VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT };
			infos[count].type = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
			infos[count].data.pTexelBuffer = &texel;
			addrs[count].address = view.storage.ptr;
			addrs[count].size = get_descriptor_size_for_type(VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER);
			count++;
		}

		auto desc_size = device->get_device_features().resource_heap_resource_desc_size;
		table.vkWriteResourceDescriptorsEXT(device->get_device(), count, infos, addrs);

		if (uniform)
			copy_uniform_texel(resource_heap.mapped + view.uniform.heap_index * desc_size, view.uniform.ptr);
		if (storage)
			copy_storage_texel(resource_heap.mapped + view.storage.heap_index * desc_size, view.storage.ptr);
	}
	else
	{
		VkDescriptorAddressInfoEXT addr = { VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT };
		VkDescriptorGetInfoEXT get_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT };

		addr.address = info.buffer->get_device_address() + info.offset;
		if (info.range == VK_WHOLE_SIZE)
			addr.range = info.buffer->get_create_info().size - info.offset;
		else
			addr.range = info.range;
		addr.format = info.format;

		VkFormatProperties3 props3 = { VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3 };
		device->get_format_properties(info.format, &props3);

		if ((info.buffer->get_create_info().usage & VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT) != 0 &&
			(props3.bufferFeatures & VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT) != 0)
		{
			view.uniform = alloc_uniform_texel();
			get_info.type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
			get_info.data.pUniformTexelBuffer = &addr;
			table.vkGetDescriptorEXT(device->get_device(), &get_info,
			                         get_descriptor_size_for_type(get_info.type), view.uniform.ptr);
		}

		if ((info.buffer->get_create_info().usage & VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT) != 0 &&
			(props3.bufferFeatures & VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT) != 0)
		{
			view.storage = alloc_storage_texel();
			get_info.type = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
			get_info.data.pStorageTexelBuffer = &addr;
			table.vkGetDescriptorEXT(
					device->get_device(), &get_info, get_descriptor_size_for_type(get_info.type), view.storage.ptr);
		}
	}

	return true;
}

void DescriptorBufferAllocator::free_buffer_view(const CachedBufferView &view)
{
	if (view.view)
		device->get_device_table().vkDestroyBufferView(device->get_device(), view.view, nullptr);

	free_cached_descriptors(&view.uniform, 1);
	free_cached_descriptors(&view.storage, 1);
}
}
