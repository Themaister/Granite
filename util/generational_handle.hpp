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
#include <stack>
#include <utility>
#include <stdint.h>
#include "object_pool.hpp"
#include <assert.h>

namespace Util
{
using GenerationalHandleID = uint32_t;
template <typename T>
class GenerationalHandlePool
{
public:
	using ID = GenerationalHandleID;

	GenerationalHandlePool()
	{
		elements.resize(16);
		generation.resize(16);
		for (unsigned i = 0; i < 16; i++)
			vacant_indices.push(i);
	}

	~GenerationalHandlePool()
	{
		for (auto &elem : elements)
			if (elem)
				pool.free(elem);
	}

	GenerationalHandlePool(const GenerationalHandlePool &) = delete;
	void operator=(const GenerationalHandlePool &) = delete;

	template <typename... P>
	ID emplace(P&&... p)
	{
		auto index = get_vacant_index();
		auto generation_index = uint8_t(++generation[index]);

		// Reserve generation index 0 for sentinel purposes.
		if (!generation_index)
			generation_index = ++generation[index];

		elements[index] = pool.allocate(std::forward<P>(p)...);
		return make_id(index, generation_index);
	}

	void remove(ID id)
	{
		auto index = memory_index(id);
		auto gen_index = generation_index(id);

		if (index >= elements.size())
			return;
		if (gen_index != generation[index])
			return;
		if (!elements[index])
			return;

		pool.free(elements[index]);
		elements[index] = nullptr;
		vacant_indices.push(index);
	}

	T *maybe_get(ID id) const
	{
		auto index = memory_index(id);
		auto gen_index = generation_index(id);

		if (index >= elements.size())
			return nullptr;
		if (gen_index != generation[index])
			return nullptr;

		return elements[index];
	}

	T &get(ID id) const
	{
		auto index = memory_index(id);
		auto gen_index = generation_index(id);

		if (index >= elements.size())
			throw std::logic_error("Invalid ID.");
		if (gen_index != generation[index])
			throw std::logic_error("Invalid ID.");
		if (!elements[index])
			throw std::logic_error("Invalid ID.");

		return *elements[index];
	}

	void clear()
	{
		for (size_t i = 0; i < elements.size(); i++)
		{
			if (elements[i])
			{
				pool.free(elements[i]);
				vacant_indices.push(i);
				elements[i] = nullptr;
			}
		}
	}

private:
	Util::ObjectPool<T> pool;
	std::vector<T *> elements;
	std::vector<uint8_t> generation;
	std::stack<uint32_t> vacant_indices;

	uint32_t get_vacant_index()
	{
		if (vacant_indices.empty())
		{
			size_t current_size = elements.size();

			// If this is ever a problem, we can bump to 64-bit IDs.
			if (current_size >= (size_t(1) << 24u))
				throw std::bad_alloc();

			elements.resize(current_size * 2);
			generation.resize(current_size * 2);
			for (size_t index = current_size; index < 2 * current_size; index++)
				vacant_indices.push(index);
		}

		auto ret = vacant_indices.top();
		vacant_indices.pop();
		return ret;
	}

	static ID make_id(uint32_t index, uint32_t generation_index)
	{
		assert(index <= 0x00ffffffu);
		assert(generation_index <= 0xffu);
		return (generation_index << 24u) | index;
	}

	static uint32_t generation_index(ID id)
	{
		return (id >> 24u) & 0xffu;
	}

	static uint32_t memory_index(ID id)
	{
		return id & 0xffffffu;
	}
};
}
