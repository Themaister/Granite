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

#include "memory_allocator.hpp"
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

void ClassAllocator::prepare_allocation(DeviceAllocation *alloc, MiniHeap &heap, const SuballocationResult &suballoc)
{
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

	VkDeviceMemory device_memory;
	VkResult res = table->vkAllocateMemory(device->get_device(), &info, nullptr, &device_memory);

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
}
