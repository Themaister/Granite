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

#include <memory>
#include <mutex>
#include <vector>
#include <stdlib.h>

namespace Util
{
template<typename T>
class ObjectPool
{
public:
	template<typename... P>
	T *allocate(P &&... p)
	{
		if (vacants.empty())
		{
			unsigned num_objects = 64u << memory.size();
			T *ptr = static_cast<T *>(malloc(num_objects * sizeof(T)));
			if (!ptr)
				return nullptr;

			for (unsigned i = 0; i < num_objects; i++)
				vacants.push_back(&ptr[i]);

			memory.emplace_back(ptr);
		}

		T *ptr = vacants.back();
		vacants.pop_back();
		new(ptr) T(std::forward<P>(p)...);
		return ptr;
	}

	void free(T *ptr)
	{
		ptr->~T();
		vacants.push_back(ptr);
	}

	void clear()
	{
		vacants.clear();
		memory.clear();
	}

protected:
	std::vector<T *> vacants;

	struct MallocDeleter
	{
		void operator()(T *ptr)
		{
			::free(ptr);
		}
	};

	std::vector<std::unique_ptr<T, MallocDeleter>> memory;
};

template<typename T>
class ThreadSafeObjectPool : private ObjectPool<T>
{
public:
	template<typename... P>
	T *allocate(P &&... p)
	{
		std::lock_guard<std::mutex> holder{lock};
		return ObjectPool<T>::allocate(std::forward<P>(p)...);
	}

	void free(T *ptr)
	{
		ptr->~T();
		std::lock_guard<std::mutex> holder{lock};
		this->vacants.push_back(ptr);
	}

	void clear()
	{
		std::lock_guard<std::mutex> holder{lock};
		ObjectPool<T>::clear();
	}

private:
	std::mutex lock;
};
}
