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

#include "hashmap.hpp"
#include "read_write_lock.hpp"
#include <memory>
#include <utility>

namespace Util
{
template <typename T>
class ThreadSafeCache
{
public:
	T *find(Hash hash) const
	{
		lock.lock_read();
		auto itr = hashmap.find(hash);
		auto *ret = itr != end(hashmap) ? itr->second.get() : nullptr;
		lock.unlock_read();
		return ret;
	}

	T *insert(Hash hash, std::unique_ptr<T> value)
	{
		lock.lock_write();
		auto &cache = hashmap[hash];
		if (!cache)
			cache = std::move(value);

		auto *ret = cache.get();
		lock.unlock_write();
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
	mutable RWSpinLock lock;
};
}