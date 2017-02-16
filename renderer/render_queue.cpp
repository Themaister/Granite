#include "render_queue.hpp"
#include <cstring>
#include <iterator>

using namespace std;

namespace Granite
{
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