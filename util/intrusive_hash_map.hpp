/* Copyright (c) 2017-2018 Hans-Kristian Arntzen
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

#include "intrusive_list.hpp"
#include "hash.hpp"
#include "object_pool.hpp"
#include <vector>
#include <assert.h>

namespace Util
{
template <typename T>
struct IntrusiveHashMapEnabled : IntrusiveListEnabled<T>
{
	Hash intrusive_hashmap_key;
};

// This HashMap is non-owning. It just arranges a list of pointers.
// It's kind of special purpose container used by the Vulkan backend.
// Dealing with memory ownership is done through composition by a different class.
// T must inherit from IntrusiveHashMapEnabled<T>.
// Each instance of T can only be part of one hashmap.

template <typename T>
class IntrusiveHashMapHolder
{
public:
	enum { InitialSize = 16 };

	IntrusiveHashMapHolder()
	{
		values.resize(InitialSize);
	}

	T *find(Hash hash) const
	{
		auto masked = hash & hash_mask;
		while (values[masked])
		{
			if (get_key_for_index(masked) == hash)
				return values[masked];
			masked = (masked + 1) & hash_mask;
		}

		return nullptr;
	}

	// Inserts, if value already exists, insertion does not happen.
	// Return value is the data which is not part of the hashmap.
	// It should be deleted or similar.
	// Returns nullptr if nothing was in the hashmap for this key.
	T *insert_yield(T *&value)
	{
		// If we grow beyond 50%, resize.
		if (count > (hash_mask >> 1))
			grow();

		auto masked = get_hash(value) & hash_mask;
		while (hash_conflict(masked, value))
			masked = (masked + 1) & hash_mask;

		if (hash_match(masked, value))
		{
			T *ret = value;
			value = values[masked];
			return ret;
		}
		else
		{
			values[masked] = value;
			list.insert_front(value);
			count++;
			return nullptr;
		}
	}

	T *insert_replace(T *value)
	{
		// If we grow beyond 50%, resize.
		if (count > (hash_mask >> 1))
			grow();

		auto masked = get_hash(value) & hash_mask;
		while (hash_conflict(masked, value))
			masked = (masked + 1) & hash_mask;

		if (hash_match(masked, value))
		{
			std::swap(values[masked], value);
			list.erase(value);
			list.insert_front(values[masked]);
			return value;
		}
		else
		{
			values[masked] = value;
			list.insert_front(value);
			count++;
			return nullptr;
		}
	}

	void erase(T *value)
	{
		auto masked = get_hash(value) & hash_mask;
		while (values[masked] && !compare_key(masked, get_hash(value)))
			masked = (masked + 1) & hash_mask;

		assert(values[masked] == value);
		assert(count > 0);
		values[masked] = nullptr;
		list.erase(value);
		count--;
	}

	void clear()
	{
		list.clear();
		values.clear();
		values.resize(InitialSize);
		hash_mask = InitialSize - 1;
	}

	typename IntrusiveList<T>::Iterator begin()
	{
		return list.begin();
	}

	typename IntrusiveList<T>::Iterator end()
	{
		return list.end();
	}

private:
	inline Hash get_key_for_index(Hash masked) const
	{
		return static_cast<IntrusiveHashMapEnabled<T> *>(values[masked])->intrusive_hashmap_key;
	}

	inline bool compare_key(Hash masked, Hash hash) const
	{
		return get_key_for_index(masked) == hash;
	}

	inline Hash get_hash(const T *value) const
	{
		return static_cast<const IntrusiveHashMapEnabled<T> *>(value)->intrusive_hashmap_key;
	}

	inline bool hash_conflict(Hash masked_hash, const T *value) const
	{
		return values[masked_hash] && !compare_key(masked_hash, get_hash(value));
	}

	inline bool hash_match(Hash masked_hash, const T *value) const
	{
		return values[masked_hash] && compare_key(masked_hash, get_hash(value));
	}

	void insert_inner(T *value)
	{
		auto masked = get_hash(value) & hash_mask;
		while (hash_conflict(masked, value))
			masked = (masked + 1) & hash_mask;
		values[masked] = value;
	}

	void grow()
	{
		for (auto &v : values)
			v = nullptr;

		values.resize(values.size() * 2);
		hash_mask = Hash(values.size()) - 1;

		// Re-insert.
		for (auto &t : list)
			insert_inner(&t);
	}

	std::vector<T *> values;
	IntrusiveList<T> list;
	Hash hash_mask = InitialSize - 1;
	size_t count = 0;
};

template <typename T>
class IntrusiveHashMap
{
public:
	T *find(Hash hash) const
	{
		return hashmap.find(hash);
	}

	void erase(T *value)
	{
		hashmap.erase(value);
		pool.free(value);
	}

	template <typename... P>
	T *emplace_replace(Hash hash, P&&... p)
	{
		T *t = allocate(std::forward<P>(p)...);
		return insert_replace(hash, t);
	}

	template <typename... P>
	T *emplace_yield(Hash hash, P&&... p)
	{
		T *t = allocate(std::forward<P>(p)...);
		return insert_yield(hash, t);
	}

	typename IntrusiveList<T>::Iterator begin()
	{
		return hashmap.begin();
	}

	typename IntrusiveList<T>::Iterator end()
	{
		return hashmap.end();
	}

private:
	IntrusiveHashMapHolder<T> hashmap;
	ObjectPool<T> pool;

	template <typename... P>
	T *allocate(P&&... p)
	{
		return pool.allocate(std::forward<P>(p)...);
	}

	T *insert_replace(Hash hash, T *value)
	{
		static_cast<IntrusiveHashMapEnabled<T> *>(value)->intrusive_hashmap_key = hash;
		T *to_delete = hashmap.insert_replace(value);
		if (to_delete)
			pool.free(to_delete);
		return value;
	}

	T *insert_yield(Hash hash, T *value)
	{
		static_cast<IntrusiveHashMapEnabled<T> *>(value)->intrusive_hashmap_key = hash;
		T *to_delete = hashmap.insert_yield(value);
		if (to_delete)
			pool.free(to_delete);
		return value;
	}
};
}