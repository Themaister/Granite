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

#pragma once

#include "image.hpp"
#include "buffer.hpp"
#include "asset_manager.hpp"
#include "meshlet.hpp"
#include "arena_allocator.hpp"
#include "small_vector.hpp"
#include <mutex>
#include <condition_variable>

namespace Vulkan
{
class MemoryMappedTexture;

namespace Internal
{
struct SliceAllocator;
struct AllocatedSlice
{
	uint32_t buffer_index = 0;
	uint32_t offset = 0;
	uint32_t count = 0;
	uint32_t mask = 0;

	SliceAllocator *alloc = nullptr;
	Util::IntrusiveList<Util::LegionHeap<AllocatedSlice>>::Iterator heap = {};
};

struct MeshGlobalAllocator
{
	explicit MeshGlobalAllocator(Device &device);
	uint32_t allocate(uint32_t count);
	void free(uint32_t index);

	enum { MaxSoACount = 3 }; // Position, attribute, skinning.

	Device &device;
	uint32_t element_size[MaxSoACount] = {};
	uint32_t soa_count = 1;
	Util::SmallVector<BufferHandle> global_buffers;
};

struct SliceAllocator : Util::ArenaAllocator<SliceAllocator, AllocatedSlice>
{
	SliceAllocator *parent = nullptr;
	MeshGlobalAllocator *global_allocator = nullptr;

	// Implements curious recurring template pattern calls.
	bool allocate_backing_heap(AllocatedSlice *allocation);
	void free_backing_heap(AllocatedSlice *allocation) const;
	void prepare_allocation(AllocatedSlice *allocation, Util::IntrusiveList<MiniHeap>::Iterator heap,
	                        const Util::SuballocationResult &suballoc);
};
}

class MeshBufferAllocator
{
public:
	explicit MeshBufferAllocator(Device &device);
	bool allocate(uint32_t count, Internal::AllocatedSlice *slice);
	void free(const Internal::AllocatedSlice &slice);
	void set_soa_count(unsigned soa_count);
	void set_element_size(unsigned soa_index, uint32_t element_size);
	uint32_t get_element_size(unsigned soa_index) const;

	const Buffer *get_buffer(unsigned index, unsigned soa_index) const;

private:
	Util::ObjectPool<Util::LegionHeap<Internal::AllocatedSlice>> object_pool;
	Internal::MeshGlobalAllocator global_allocator;
	enum { SliceAllocatorCount = 4 };
	Internal::SliceAllocator allocators[SliceAllocatorCount];
};

class ResourceManager final : private Granite::AssetInstantiatorInterface
{
public:
	explicit ResourceManager(Device *device);
	~ResourceManager() override;
	void init();

	enum class MeshEncoding
	{
		Meshlet,
		EncodedVBOAndIBO,
	};

	inline const Vulkan::ImageView *get_image_view(Granite::AssetID id) const
	{
		if (id.id < views.size())
			return views[id.id];
		else
			return nullptr;
	}

	const Vulkan::ImageView *get_image_view_blocking(Granite::AssetID id);

	inline VkDrawIndexedIndirectCommand get_mesh_indexed_draw(Granite::AssetID id) const
	{
		if (id.id < draws.size())
			return draws[id.id];
		else
			return {};
	}

	inline MeshEncoding get_mesh_encoding() const
	{
		return MeshEncoding::EncodedVBOAndIBO;
	}

	const Buffer *get_index_buffer() const;
	const Buffer *get_position_buffer() const;
	const Buffer *get_attribute_buffer() const;
	const Buffer *get_skinning_buffer() const;
	const Buffer *get_indirect_buffer() const;

private:
	Device *device;
	Granite::AssetManager *manager = nullptr;

	void latch_handles() override;
	uint64_t estimate_cost_asset(Granite::AssetID id, Granite::File &file) override;
	void instantiate_asset(Granite::AssetManager &manager, Granite::TaskGroup *task,
	                       Granite::AssetID id, Granite::File &file) override;
	void release_asset(Granite::AssetID id) override;
	void set_id_bounds(uint32_t bound) override;
	void set_asset_class(Granite::AssetID id, Granite::AssetClass asset_class) override;

	struct Asset
	{
		ImageHandle image;
		struct
		{
			Internal::AllocatedSlice index, attr, indirect;
		} mesh;
		Granite::AssetClass asset_class = Granite::AssetClass::ImageZeroable;
		bool latchable = false;
	};

	std::mutex lock;
	std::condition_variable cond;

	std::vector<Asset> assets;
	std::vector<const ImageView *> views;
	std::vector<VkDrawIndexedIndirectCommand> draws;
	std::vector<Granite::AssetID> updates;

	ImageHandle fallback_color;
	ImageHandle fallback_normal;
	ImageHandle fallback_zero;
	ImageHandle fallback_pbr;

	ImageHandle create_gtx(Granite::FileMappingHandle mapping, Granite::AssetID id);
	ImageHandle create_gtx(const MemoryMappedTexture &mapping, Granite::AssetID id);
	ImageHandle create_other(const Granite::FileMapping &mapping, Granite::AssetClass asset_class, Granite::AssetID id);
	const ImageHandle &get_fallback_image(Granite::AssetClass asset_class);

	void instantiate_asset(Granite::AssetManager &manager, Granite::AssetID id, Granite::File &file);
	void instantiate_asset_image(Granite::AssetManager &manager, Granite::AssetID id, Granite::File &file);
	void instantiate_asset_mesh(Granite::AssetManager &manager, Granite::AssetID id, Granite::File &file);

	std::mutex mesh_allocator_lock;
	MeshBufferAllocator index_buffer_allocator;
	MeshBufferAllocator attribute_buffer_allocator;
	MeshBufferAllocator indirect_buffer_allocator;

	bool allocate_asset_mesh(Granite::AssetID id, const Meshlet::MeshView &view);
};
}
