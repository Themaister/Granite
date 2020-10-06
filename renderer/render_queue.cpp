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

#include "render_queue.hpp"
#include "render_context.hpp"
#include <cstring>
#include <iterator>
#include <algorithm>
#include <assert.h>

using namespace std;
using namespace Vulkan;
using namespace Util;

namespace Granite
{
void RenderQueue::sort()
{
	for (auto &queue : queues)
	{
		stable_sort(begin(queue), end(queue), [](const RenderQueueData &a, const RenderQueueData &b) {
			return a.sorting_key < b.sorting_key;
		});
	}
}

void RenderQueue::combine_render_info(const RenderQueue &queue)
{
	for (unsigned i = 0; i < ecast(Queue::Count); i++)
	{
		auto e = static_cast<Queue>(i);
		queues[i].insert(end(queues[i]), begin(queue.get_queue_data(e)), end(queue.get_queue_data(e)));
	}
}

void RenderQueue::dispatch_range(Queue queue_type, CommandBuffer &cmd, const CommandBufferSavedState *state, size_t begin, size_t end) const
{
	auto *queue = queues[ecast(queue_type)].data();

	while (begin < end)
	{
		if (state)
			cmd.restore_state(*state);

		unsigned instances = 1;
		for (size_t i = begin + 1; i < end && queue[i].render_info == queue[begin].render_info; i++)
		{
			assert(queue[i].render == queue[begin].render);
			instances++;
		}

		queue[begin].render(cmd, &queue[begin], instances);
		begin += instances;
	}
}

size_t RenderQueue::get_dispatch_size(Queue queue) const
{
	return queues[ecast(queue)].size();
}

void RenderQueue::dispatch(Queue queue, CommandBuffer &cmd, const CommandBufferSavedState *state) const
{
	dispatch_range(queue, cmd, state, 0, queues[ecast(queue)].size());
}

void RenderQueue::dispatch_subset(Queue queue, Vulkan::CommandBuffer &cmd, const Vulkan::CommandBufferSavedState *state,
                                  unsigned index, unsigned num_indices) const
{
	size_t size = get_dispatch_size(queue);
	size_t begin_index = (size * index) / num_indices;
	size_t end_index = (size * (index + 1)) / num_indices;
	dispatch_range(queue, cmd, state, begin_index, end_index);
}

void RenderQueue::enqueue_queue_data(Queue queue_type, const RenderQueueData &render_info)
{
	queues[ecast(queue_type)].push_back(render_info);
}

Util::ThreadSafeObjectPool<RenderQueue::Block> RenderQueue::allocator_pool;

RenderQueue::Block *RenderQueue::insert_block()
{
	auto *ret = allocator_pool.allocate();
	blocks.push_back(ret);
	return ret;
}

RenderQueue::Block *RenderQueue::insert_large_block(size_t size, size_t alignment)
{
	size_t padded_size = alignment > alignof(uintmax_t) ? (size + alignment) : size;
	auto *ret = allocator_pool.allocate(padded_size);
	blocks.push_back(ret);
	return ret;
}

void *RenderQueue::allocate_from_block(Block &block, size_t size, size_t alignment)
{
	block.ptr = (block.ptr + alignment - 1) & ~(alignment - 1);
	uintptr_t end = block.ptr + size;
	if (end <= block.end)
	{
		void *ret = reinterpret_cast<void *>(block.ptr);
		block.ptr = end;
		return ret;
	}
	else
		return nullptr;
}

void RenderQueue::recycle_blocks()
{
	for (auto *block : blocks)
		allocator_pool.free(block);
	blocks.clear();
	current = nullptr;
}

void RenderQueue::reset()
{
	recycle_blocks();
	for (auto &queue : queues)
		queue.clear();
	render_infos.clear();
}

RenderQueue::~RenderQueue()
{
	recycle_blocks();
}

void *RenderQueue::allocate(size_t size, size_t alignment)
{
	if (size + alignment > BlockSize)
	{
		auto *block = insert_large_block(size, alignment);
		return allocate_from_block(*block, size, alignment);
	}

	// First allocation.
	if (!current)
		current = insert_block();

	void *data = allocate_from_block(*current, size, alignment);
	if (data)
		return data;

	current = insert_block();
	data = allocate_from_block(*current, size, alignment);
	return data;
}

void RenderQueue::push_renderables(const RenderContext &context, const VisibilityList &visible)
{
	for (auto &vis : visible)
		vis.renderable->get_render_info(context, vis.transform, *this);
}

void RenderQueue::push_depth_renderables(const RenderContext &context, const VisibilityList &visible)
{
	for (auto &vis : visible)
		vis.renderable->get_depth_render_info(context, vis.transform, *this);
}

uint64_t RenderInfo::get_background_sort_key(Queue queue_type, Util::Hash pipeline_hash, Util::Hash draw_hash)
{
	pipeline_hash &= 0xffff0000u;
	pipeline_hash |= draw_hash & 0xffffu;

	if (queue_type == Queue::Transparent)
		return pipeline_hash & 0xffffffffu;
	else
		return (UINT64_MAX << 32) | (pipeline_hash & 0xffffffffu);
}

uint64_t RenderInfo::get_sprite_sort_key(Queue queue_type, Util::Hash pipeline_hash, Util::Hash draw_hash,
                                         float z, StaticLayer layer)
{
	static_assert(ecast(StaticLayer::Count) == 4, "Number of static layers is not 4.");

	// Monotonically increasing floating point will be monotonic in uint32_t as well when z is non-negative.
	z = muglm::max(z, 0.0f);
	uint32_t depth_key = floatBitsToUint(z);

	pipeline_hash &= 0xffff0000u;
	pipeline_hash |= draw_hash & 0xffffu;

	if (queue_type == Queue::Transparent)
	{
		depth_key ^= 0xffffffffu; // Back-to-front instead.
		// Prioritize correct back-to-front rendering over pipeline.
		return (uint64_t(depth_key) << 32) | pipeline_hash;
	}
	else
	{
#if 1
		// Prioritize state changes over depth.
		depth_key >>= 2;
		return (uint64_t(ecast(layer)) << 62) | (uint64_t(pipeline_hash) << 30) | depth_key;
#else
		// Prioritize front-back sorting over state changes.
		pipeline_hash >>= 2;
		return (uint64_t(ecast(layer)) << 62) | (uint64_t(depth_key) << 30) | pipeline_hash;
#endif
	}
}

uint64_t RenderInfo::get_sort_key(const RenderContext &context, Queue queue_type, Util::Hash pipeline_hash,
                                  Util::Hash draw_hash,
                                  const vec3 &center, StaticLayer layer)
{
	float z = dot(context.get_render_parameters().camera_front, center - context.get_render_parameters().camera_position);
	return get_sprite_sort_key(queue_type, pipeline_hash, draw_hash, z, layer);
}
}
