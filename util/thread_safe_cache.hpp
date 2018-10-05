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

#include "hashmap.hpp"
#include "read_write_lock.hpp"
#include <memory>
#include <utility>

namespace Util
{
template <typename T>
class Cache
{
public:
	T *find(Hash hash) const
	{
		auto itr = hashmap.find(hash);
		auto *ret = itr != end(hashmap) ? itr->second.get() : nullptr;
		return ret;
	}

	T *insert(Hash hash, std::unique_ptr<T> value)
	{
		auto &cache = hashmap[hash];
		if (!cache)
			cache = std::move(value);

		auto *ret = cache.get();
		return ret;
	}

	T *insert_replace(Hash hash, std::unique_ptr<T> value)
	{
		auto &cache = hashmap[hash];
		cache = std::move(value);

		auto *ret = cache.get();
		return ret;
	}

	HashMap<std::unique_ptr<T>> &get_hashmap()
	{
		return hashmap;
	}

	const HashMap<std::unique_ptr<T>> &get_hashmap() const
	{
		return hashmap;
	}

private:
	HashMap<std::unique_ptr<T>> hashmap;
};

template <typename T>
class ThreadSafeCache
{
public:
	T *find(Hash hash) const
	{
		lock.lock_read();
		auto *ret = cache.find(hash);
		lock.unlock_read();
		return ret;
	}

	T *insert(Hash hash, std::unique_ptr<T> value)
	{
		lock.lock_write();
		auto *ret = cache.insert(hash, std::move(value));
		lock.unlock_write();
		return ret;
	}

	T *insert_replace(Hash hash, std::unique_ptr<T> value)
	{
		lock.lock_write();
		auto *ret = cache.insert_replace(hash, std::move(value));
		lock.unlock_write();
		return ret;
	}

	HashMap<std::unique_ptr<T>> &get_hashmap()
	{
		return cache.get_hashmap();
	}

	const HashMap<std::unique_ptr<T>> &get_hashmap() const
	{
		return cache.get_hashmap();
	}

private:
	Cache<T> cache;
	mutable RWSpinLock lock;
};
}