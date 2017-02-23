#include "render_queue.hpp"
#include <cstring>
#include <iterator>
#include <algorithm>
#include <assert.h>

using namespace std;
using namespace Vulkan;

namespace Granite
{
void RenderQueue::sort()
{
	stable_sort(queue, queue + count, [](const RenderInfo *a, const RenderInfo *b) {
		return a->sorting_key < b->sorting_key;
	});
}

void RenderQueue::combine_render_info(const RenderQueue &queue)
{
	size_t n = queue.get_queue_count();
	auto **other_infos = queue.get_queue();
	for (size_t i = 0; i < n; i++)
		enqueue(other_infos[i]);
}

void RenderQueue::dispatch(CommandBuffer &cmd, size_t begin, size_t end)
{
	while (begin < end)
	{
		assert(queue[begin]->instance_key != 0);
		assert(queue[begin]->sorting_key != 0);

		unsigned instances = 1;
		for (size_t i = begin + 1; i < end && queue[i]->instance_key == queue[begin]->instance_key; i++)
		{
			assert(queue[i]->render == queue[begin]->render);
			instances++;
		}

		queue[begin]->render(cmd, &queue[begin], instances);
		begin += instances;
	}
}

void RenderQueue::dispatch(CommandBuffer &cmd)
{
	dispatch(cmd, 0, count);
}

void RenderQueue::enqueue(RenderInfo *render_info)
{
	if (count >= capacity)
	{
		size_t new_capacity = capacity ? capacity * 2 : 64;
		RenderInfo **new_queue = static_cast<RenderInfo **>(allocate(sizeof(RenderInfo *) * new_capacity, alignof(RenderInfo *)));
		memcpy(new_queue, queue, count * sizeof(*new_queue));
		queue = new_queue;
		capacity = new_capacity;
	}
	queue[count++] = render_info;
}

RenderQueue::Chain::iterator RenderQueue::insert_block()
{
	return blocks.insert(end(blocks), Block(BlockSize));
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

void RenderQueue::reset()
{
	current = begin(blocks);
	if (current != end(blocks))
		current->reset();

	queue = nullptr;
	count = 0;
	capacity = 0;
}

void RenderQueue::reset_and_reclaim()
{
	blocks.clear();
	current = end(blocks);

	queue = nullptr;
	count = 0;
	capacity = 0;
}

void *RenderQueue::allocate(size_t size, size_t alignment)
{
	if (size + alignment > BlockSize)
		return nullptr;

	// First allocation.
	if (current == end(blocks))
		current = insert_block();

	void *data = allocate_from_block(*current, size, alignment);
	if (data)
		return data;

	++current;
	if (current == end(blocks))
		current = insert_block();
	else
		current->reset();

	data = allocate_from_block(*current, size, alignment);
	return data;
}
}