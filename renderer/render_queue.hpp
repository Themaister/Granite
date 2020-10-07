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

#pragma once

#include <vector>
#include <list>
#include <type_traits>
#include <stdexcept>
#include "command_buffer.hpp"
#include "hash.hpp"
#include "enum_cast.hpp"
#include "intrusive_hash_map.hpp"
#include "math.hpp"

namespace Granite
{
class ShaderSuite;
class RenderContext;
class AbstractRenderable;
class PositionalLight;
struct RenderInfoComponent;

struct RenderableInfo
{
	AbstractRenderable *renderable;
	const RenderInfoComponent *transform;
	Util::Hash transform_hash;
};

struct PositionalLightInfo
{
	PositionalLight *light;
	const RenderInfoComponent *transform;
	Util::Hash transform_hash;
};
using VisibilityList = std::vector<RenderableInfo>;
using PositionalLightList = std::vector<PositionalLightInfo>;

enum class Queue : unsigned
{
	Opaque = 0,
	OpaqueEmissive,
	Light, // Relevant only for classic deferred rendering
	Transparent,
	Count
};

enum class StaticLayer : unsigned
{
	Front,
	Default,
	Back,
	Last,
	Count
};

struct RenderInfo
{
	static uint64_t get_sort_key(const RenderContext &context, Queue queue_type,
	                             Util::Hash pipeline_hash, Util::Hash draw_hash,
	                             const vec3 &center,
	                             StaticLayer layer = StaticLayer::Default);
	static uint64_t get_sprite_sort_key(Queue queue_type,
	                                    Util::Hash pipeline_hash, Util::Hash draw_hash,
	                                    float layer, StaticLayer static_layer = StaticLayer::Default);
	static uint64_t get_background_sort_key(Queue queue_type, Util::Hash pipeline_hash, Util::Hash draw_hash);

private:
	RenderInfo() = default;
};

struct RenderQueueData;
using RenderFunc = void (*)(Vulkan::CommandBuffer &, const RenderQueueData *, unsigned);

struct RenderQueueData
{
	// How to render an object.
	RenderFunc render;

	// Per-draw call specific data. Understood by the render callback.
	const void *render_info;

	// Per-instance specific data. Understood by the render callback.
	const void *instance_data;

	// Sorting key.
	// Lower sorting keys will appear earlier.
	uint64_t sorting_key;
};

struct QueueDataWrappedErased : Util::IntrusiveHashMapEnabled<QueueDataWrappedErased>
{
};

template <typename T>
struct QueueDataWrapped : QueueDataWrappedErased
{
	T data;
};

class RenderQueue
{
public:
	enum { BlockSize = 64 * 1024 };

	RenderQueue() = default;
	void operator=(const RenderQueue &) = delete;
	RenderQueue(const RenderQueue &) = delete;
	~RenderQueue();

	template <typename T>
	T *push(Queue queue, Util::Hash instance_key, uint64_t sorting_key,
	        RenderFunc render, void *instance_data)
	{
		static_assert(std::is_trivially_destructible<T>::value, "Dispatchable type is not trivially destructible!");

		assert(instance_key != 0);
		assert(sorting_key != 0);

		Util::Hasher h(instance_key);
		h.pointer(render);

		using WrappedT = QueueDataWrapped<T>;

		auto *itr = render_infos.find(h.get());
		if (itr)
		{
			auto *t = static_cast<WrappedT *>(itr);
			enqueue_queue_data(queue, { render, &t->data, instance_data, sorting_key });
			return nullptr;
		}
		else
		{
			void *buffer = allocate(sizeof(WrappedT), alignof(WrappedT));
			if (!buffer)
				throw std::bad_alloc();

			auto *t = new(buffer) WrappedT();
			t->set_hash(h.get());
			render_infos.insert_replace(t);
			enqueue_queue_data(queue, { render, &t->data, instance_data, sorting_key });
			return &t->data;
		}
	}

	void *allocate(size_t size, size_t alignment = 64);

	template <typename T>
	T *allocate_one()
	{
		static_assert(std::is_trivially_destructible<T>::value, "Type is not trivially destructible!");
		return static_cast<T *>(allocate(sizeof(T), alignof(T)));
	}

	template <typename T>
	T *allocate_many(size_t n)
	{
		static_assert(std::is_trivially_destructible<T>::value, "Type is not trivially destructible!");
		return static_cast<T *>(allocate(sizeof(T) * n, alignof(T)));
	}

	void combine_render_info(const RenderQueue &queue);
	void reset();

	using RenderQueueDataVector = Util::SmallVector<RenderQueueData, 64>;

	const RenderQueueDataVector &get_queue_data(Queue queue) const
	{
		return queues[Util::ecast(queue)];
	}

	void sort();
	void dispatch(Queue queue, Vulkan::CommandBuffer &cmd, const Vulkan::CommandBufferSavedState *state) const;
	void dispatch_range(Queue queue, Vulkan::CommandBuffer &cmd, const Vulkan::CommandBufferSavedState *state, size_t begin, size_t end) const;
	void dispatch_subset(Queue queue, Vulkan::CommandBuffer &cmd, const Vulkan::CommandBufferSavedState *state, unsigned index, unsigned num_indices) const;
	size_t get_dispatch_size(Queue queue) const;

	void set_shader_suites(ShaderSuite *suite)
	{
		shader_suites = suite;
	}

	ShaderSuite *get_shader_suites() const
	{
		return shader_suites;
	}

	void push_renderables(const RenderContext &context, const VisibilityList &visible);
	void push_depth_renderables(const RenderContext &context, const VisibilityList &visible);

private:
	void enqueue_queue_data(Queue queue, const RenderQueueData &data);

	struct Block : Util::IntrusivePtrEnabled<Block>
	{
		std::unique_ptr<uint8_t[]> large_buffer;
		uintptr_t ptr = 0;
		uintptr_t begin = 0;
		uintptr_t end = 0;
		uint8_t inline_buffer[BlockSize];

		explicit Block(size_t size)
		{
			large_buffer.reset(new uint8_t[size]);
			begin = reinterpret_cast<uintptr_t>(large_buffer.get());
			end = reinterpret_cast<uintptr_t>(large_buffer.get()) + size;
			reset();
		}

		Block()
		{
			begin = reinterpret_cast<uintptr_t>(inline_buffer);
			end = reinterpret_cast<uintptr_t>(inline_buffer) + BlockSize;
			reset();
		}

		void operator=(const Block &) = delete;
		Block(const Block &) = delete;

		void reset()
		{
			ptr = begin;
		}
	};

	static Util::ThreadSafeObjectPool<Block> allocator_pool;

	static void *allocate_from_block(Block &block, size_t size, size_t alignment);
	Block *insert_block();
	Block *insert_large_block(size_t size, size_t alignment);

	RenderQueueDataVector queues[static_cast<unsigned>(Queue::Count)];
	Util::SmallVector<Block *, 64> blocks;
	Block *current = nullptr;

	ShaderSuite *shader_suites = nullptr;
	Util::IntrusiveHashMapHolder<QueueDataWrappedErased> render_infos;
	void recycle_blocks();
};
}
