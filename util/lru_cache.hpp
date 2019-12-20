/* Copyright (c) 2017-2019 Hans-Kristian Arntzen
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

#include "object_pool.hpp"
#include "intrusive_list.hpp"
#include "intrusive_hash_map.hpp"

namespace Util
{
template <typename T>
class LRUCache
{
public:
	void set_total_cost(uint64_t cost)
	{
		total_cost_limit = cost;
	}

	T *find_and_mark_as_recent(uint64_t cookie)
	{
		auto *entry = hashmap.find(get_hash(cookie));
		if (entry)
		{
			lru.move_to_front(lru, entry->get());
			return &entry->get()->t;
		}
		else
			return nullptr;
	}

	T *allocate(uint64_t cookie, uint64_t cost)
	{
		auto *t = hashmap.find(get_hash(cookie));
		if (t)
		{
			total_cost -= t->cost;
			total_cost += cost;
			t->cost = cost;
			lru.move_to_front(lru, t->get());
			return t;
		}

		total_cost += cost;
		auto *entry = pool.allocate();
		lru.insert_front(entry);
		hashmap.insert_replace(get_hash(cookie), lru.begin());
		return &entry->t;
	}

	void prune()
	{
		while (total_cost > total_cost_limit)
		{
			auto itr = lru.rbegin();
			total_cost -= itr->cost;
			lru.erase(itr);
			hashmap.erase(itr);
			pool.free(itr.get());
		}
	}

	void evict(uint64_t cookie)
	{
		auto *entry = hashmap.find(get_hash(cookie));
		if (entry)
			lru.move_to_back(lru, entry->get());
	}

	bool erase(uint64_t cookie)
	{
		auto *entry = hashmap.find(get_hash(cookie));
		if (entry)
		{
			hashmap.erase(entry);
			lru.erase(entry->get());
			pool.free(entry->get());
			return true;
		}
		else
			return false;
	}

	~LRUCache()
	{
		while (!lru.empty())
		{
			auto itr = lru.begin();
			lru.erase(itr);
			pool.free(itr);
		}
	}

private:
	struct CacheEntry : IntrusiveListEnabled<CacheEntry>
	{
		uint64_t cost;
		T t;
	};

	uint64_t total_cost = 0;
	uint64_t total_cost_limit = 0;

	ObjectPool<CacheEntry> pool;
	IntrusiveList<CacheEntry> lru;
	IntrusiveHashMap<IntrusivePODWrapper<typename IntrusiveList<CacheEntry>::iterator>> hashmap;

	static Hash get_hash(uint64_t cookie)
	{
		Hasher h;
		h.u64(cookie);
		return h.get();
	}
};
}
