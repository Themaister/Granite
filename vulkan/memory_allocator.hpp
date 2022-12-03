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

#pragma once

#include "intrusive.hpp"
#include "object_pool.hpp"
#include "intrusive_list.hpp"
#include "vulkan_headers.hpp"
#include "logging.hpp"
#include "bitops.hpp"
#include "enum_cast.hpp"
#include "vulkan_common.hpp"
#include "arena_allocator.hpp"
#include <assert.h>
#include <memory>
#include <stddef.h>
#include <stdint.h>
#include <vector>

namespace Vulkan
{
class Device;

enum class MemoryClass : uint8_t
{
	Small = 0,
	Medium,
	Large,
	Huge,
	Count
};

enum class AllocationMode : uint8_t
{
	LinearHostMappable = 0,
	LinearDevice,
	LinearDeviceHighPriority,
	OptimalResource,
	OptimalRenderTarget,
	External,
	Count
};

enum MemoryAccessFlag : uint32_t
{
	MEMORY_ACCESS_WRITE_BIT = 1,
	MEMORY_ACCESS_READ_BIT = 2,
	MEMORY_ACCESS_READ_WRITE_BIT = MEMORY_ACCESS_WRITE_BIT | MEMORY_ACCESS_READ_BIT
};
using MemoryAccessFlags = uint32_t;

struct DeviceAllocation;
class DeviceAllocator;

class ClassAllocator;
class DeviceAllocator;
class Allocator;
class Device;

using MiniHeap = Util::LegionHeap<DeviceAllocation>;

struct DeviceAllocation
{
	friend class Util::ArenaAllocator<ClassAllocator, DeviceAllocation>;
	friend class ClassAllocator;
	friend class Allocator;
	friend class DeviceAllocator;
	friend class Device;
	friend class ImageResourceHolder;

public:
	inline VkDeviceMemory get_memory() const
	{
		return base;
	}

	inline bool allocation_is_global() const
	{
		return !alloc && base;
	}

	inline uint32_t get_offset() const
	{
		return offset;
	}

	inline uint32_t get_size() const
	{
		return size;
	}

	inline uint32_t get_mask() const
	{
		return mask;
	}

	inline bool is_host_allocation() const
	{
		return host_base != nullptr;
	}

	static DeviceAllocation make_imported_allocation(VkDeviceMemory memory, VkDeviceSize size, uint32_t memory_type);

	ExternalHandle export_handle(Device &device);

private:
	VkDeviceMemory base = VK_NULL_HANDLE;
	uint8_t *host_base = nullptr;
	ClassAllocator *alloc = nullptr;
	Util::IntrusiveList<MiniHeap>::Iterator heap = {};
	uint32_t offset = 0;
	uint32_t mask = 0;
	uint32_t size = 0;
	VkExternalMemoryHandleTypeFlags exportable_types = 0;

	AllocationMode mode = AllocationMode::Count;
	uint8_t memory_type = 0;

	void free_global(DeviceAllocator &allocator, uint32_t size, uint32_t memory_type);
	void free_immediate();
	void free_immediate(DeviceAllocator &allocator);
};

class DeviceAllocationOwner;
struct DeviceAllocationDeleter
{
	void operator()(DeviceAllocationOwner *owner);
};

class DeviceAllocationOwner : public Util::IntrusivePtrEnabled<DeviceAllocationOwner, DeviceAllocationDeleter, HandleCounter>
{
public:
	friend class Util::ObjectPool<DeviceAllocationOwner>;
	friend struct DeviceAllocationDeleter;

	~DeviceAllocationOwner();
	const DeviceAllocation &get_allocation() const;

private:
	DeviceAllocationOwner(Device *device, const DeviceAllocation &alloc);
	Device *device;
	DeviceAllocation alloc;
};
using DeviceAllocationOwnerHandle = Util::IntrusivePtr<DeviceAllocationOwner>;

struct MemoryAllocateInfo
{
	VkMemoryRequirements requirements = {};
	VkMemoryPropertyFlags required_properties = 0;
	AllocationMode mode = {};
};

class ClassAllocator : public Util::ArenaAllocator<ClassAllocator, DeviceAllocation>
{
public:
	friend class Util::ArenaAllocator<ClassAllocator, DeviceAllocation>;

	inline void set_global_allocator(DeviceAllocator *allocator, AllocationMode mode, uint32_t memory_type_)
	{
		global_allocator = allocator;
		global_allocator_mode = mode;
		memory_type = memory_type_;
	}

	inline void set_parent(ClassAllocator *allocator)
	{
		parent = allocator;
	}

private:
	ClassAllocator *parent = nullptr;
	uint32_t memory_type = 0;
	DeviceAllocator *global_allocator = nullptr;
	AllocationMode global_allocator_mode = AllocationMode::Count;

	// Implements curious recurring template pattern calls.
	bool allocate_backing_heap(DeviceAllocation *allocation);
	void free_backing_heap(DeviceAllocation *allocation);
	void prepare_allocation(DeviceAllocation *allocation, MiniHeap &heap, const SuballocationResult &suballoc);
};

class Allocator
{
public:
	explicit Allocator(Util::ObjectPool<MiniHeap> &object_pool);
	void operator=(const Allocator &) = delete;
	Allocator(const Allocator &) = delete;

	bool allocate(uint32_t size, uint32_t alignment, AllocationMode mode, DeviceAllocation *alloc);
	bool allocate_global(uint32_t size, AllocationMode mode, DeviceAllocation *alloc);
	bool allocate_dedicated(uint32_t size, AllocationMode mode, DeviceAllocation *alloc,
	                        VkObjectType object_type, uint64_t object, ExternalHandle *external);

	inline ClassAllocator &get_class_allocator(MemoryClass clazz, AllocationMode mode)
	{
		return classes[unsigned(clazz)][unsigned(mode)];
	}

	static void free(DeviceAllocation *alloc)
	{
		alloc->free_immediate();
	}

	void set_global_allocator(DeviceAllocator *allocator, uint32_t memory_type_)
	{
		memory_type = memory_type_;
		for (auto &sub : classes)
			for (int i = 0; i < Util::ecast(AllocationMode::Count); i++)
				sub[i].set_global_allocator(allocator, AllocationMode(i), memory_type);
		global_allocator = allocator;
	}

private:
	ClassAllocator classes[Util::ecast(MemoryClass::Count)][Util::ecast(AllocationMode::Count)];
	DeviceAllocator *global_allocator = nullptr;
	uint32_t memory_type = 0;
};

struct HeapBudget
{
	VkDeviceSize max_size;
	VkDeviceSize budget_size;
	VkDeviceSize tracked_usage;
	VkDeviceSize device_usage;
};

class DeviceAllocator
{
public:
	void init(Device *device);

	~DeviceAllocator();

	bool allocate_generic_memory(uint32_t size, uint32_t alignment, AllocationMode mode, uint32_t memory_type,
	                             DeviceAllocation *alloc);
	bool allocate_buffer_memory(uint32_t size, uint32_t alignment, AllocationMode mode, uint32_t memory_type,
	                            VkBuffer buffer, DeviceAllocation *alloc, ExternalHandle *external);
	bool allocate_image_memory(uint32_t size, uint32_t alignment, AllocationMode mode, uint32_t memory_type,
	                           VkImage image, bool force_no_dedicated, DeviceAllocation *alloc, ExternalHandle *external);

	void garbage_collect();
	void *map_memory(const DeviceAllocation &alloc, MemoryAccessFlags flags, VkDeviceSize offset, VkDeviceSize length);
	void unmap_memory(const DeviceAllocation &alloc, MemoryAccessFlags flags, VkDeviceSize offset, VkDeviceSize length);

	void get_memory_budget(HeapBudget *heaps);

	bool internal_allocate(uint32_t size, uint32_t memory_type, AllocationMode mode,
	                       VkDeviceMemory *memory, uint8_t **host_memory,
	                       VkObjectType object_type, uint64_t dedicated_object, ExternalHandle *external);
	void internal_free(uint32_t size, uint32_t memory_type, AllocationMode mode, VkDeviceMemory memory, bool is_mapped);
	void internal_free_no_recycle(uint32_t size, uint32_t memory_type, VkDeviceMemory memory);

private:
	Util::ObjectPool<MiniHeap> object_pool;
	std::vector<std::unique_ptr<Allocator>> allocators;
	Device *device = nullptr;
	const VolkDeviceTable *table = nullptr;
	VkPhysicalDeviceMemoryProperties mem_props;
	VkDeviceSize atom_alignment = 1;
	struct Allocation
	{
		VkDeviceMemory memory;
		uint32_t size;
		uint32_t type;
		AllocationMode mode;
	};

	struct Heap
	{
		uint64_t size = 0;
		std::vector<Allocation> blocks;
		void garbage_collect(Device *device);
	};

	std::vector<Heap> heaps;
	bool memory_heap_is_budget_critical[VK_MAX_MEMORY_HEAPS] = {};
	void get_memory_budget_nolock(HeapBudget *heaps);
};
}
