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

struct RenderQueueData
{
	// How to render an object.
	void (*render)(Vulkan::CommandBuffer &cmd, const RenderQueueData *infos, unsigned instance_count);

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
	        void (*render)(Vulkan::CommandBuffer &cmd, const RenderQueueData *infos, unsigned instance_data),
	        void *instance_data)
	{
		static_assert(std::is_trivially_destructible<T>::value, "Dispatchable type is not trivially destructible!");
		static_assert(std::is_trivially_copyable<T>::value, "Dispatchable type is not trivially copyable!");

		assert(instance_key != 0);
		assert(sorting_key != 0);

		auto itr = render_infos.find(instance_key);
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
			enqueue_queue_data(queue, { render, t, instance_data, sorting_key });
			return t;
		}
	}

	void *allocate(size_t size, size_t alignment = 64);

	template <typename T>
	T *allocate_one()
	{
		static_assert(std::is_trivially_destructible<T>::value, "Type is not trivially destructible!");
		static_assert(std::is_trivially_copyable<T>::value, "Type is not trivially copyable!");
		return static_cast<T *>(allocate(sizeof(T), alignof(T)));
	}

	template <typename T>
	T *allocate_many(size_t n)
	{
		static_assert(std::is_trivially_destructible<T>::value, "Type is not trivially destructible!");
		static_assert(std::is_trivially_copyable<T>::value, "Type is not trivially copyable!");
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
	Util::HashMap<RenderQueueData *> render_infos;
};
}
