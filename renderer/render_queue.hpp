/* Copyright (c) 2017 Hans-Kristian Arntzen
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
#include <command_buffer.hpp>
#include "hashmap.hpp"
#include "enum_cast.hpp"
#include "math.hpp"

namespace Granite
{
class ShaderSuite;
class RenderContext;

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

class RenderQueue
{
public:
	enum { BlockSize = 256 * 1024 };

	template <typename T>
	T *push(Queue queue, Util::Hash instance_key, uint64_t sorting_key,
	        RenderFunc render, void *instance_data)
	{
		static_assert(std::is_trivially_destructible<T>::value, "Dispatchable type is not trivially destructible!");

		assert(instance_key != 0);
		assert(sorting_key != 0);

		Util::Hasher h(instance_key);
		h.pointer(render);

		auto itr = render_infos.find(h.get());
		if (itr != std::end(render_infos))
		{
			enqueue_queue_data(queue, { render, itr->second, instance_data, sorting_key });
			return nullptr;
		}
		else
		{
			void *buffer = allocate(sizeof(T), alignof(T));
			if (!buffer)
				throw std::bad_alloc();

			T *t = new(buffer) T();
			render_infos[h.get()] = t;
			enqueue_queue_data(queue, { render, t, instance_data, sorting_key });
			return t;
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
	void reset_and_reclaim();

	const std::vector<RenderQueueData> &get_queue_data(Queue queue) const
	{
		return queues[Util::ecast(queue)];
	}

	void sort();
	void dispatch(Queue queue, Vulkan::CommandBuffer &cmd, const Vulkan::CommandBufferSavedState *state);
	void dispatch(Queue queue, Vulkan::CommandBuffer &cmd, const Vulkan::CommandBufferSavedState *state, size_t begin, size_t end);

	void set_shader_suites(ShaderSuite *suite)
	{
		shader_suites = suite;
	}

	ShaderSuite *get_shader_suites() const
	{
		return shader_suites;
	}

private:
	void enqueue_queue_data(Queue queue, const RenderQueueData &data);

	struct Block
	{
		std::vector<uint8_t> buffer;
		uintptr_t ptr = 0;
		uintptr_t begin = 0;
		uintptr_t end = 0;

		Block(size_t size)
		{
			buffer.resize(size);
			begin = reinterpret_cast<uintptr_t>(buffer.data());
			end = reinterpret_cast<uintptr_t>(buffer.data()) + size;
			reset();
		}

		void operator=(const Block &) = delete;
		Block(const Block &) = delete;
		Block(Block &&) = default;
		Block &operator=(Block &&) = default;

		void reset()
		{
			ptr = begin;
		}
	};

	using Chain = std::list<Block>;
	Chain blocks;
	Chain large_blocks;
	Chain::iterator current = std::end(blocks);

	std::vector<RenderQueueData> queues[static_cast<unsigned>(Queue::Count)];

	void *allocate_from_block(Block &block, size_t size, size_t alignment);
	Chain::iterator insert_block();
	Chain::iterator insert_large_block(size_t size, size_t alignment);

	ShaderSuite *shader_suites = nullptr;
	Util::HashMap<void *> render_infos;
};
}
