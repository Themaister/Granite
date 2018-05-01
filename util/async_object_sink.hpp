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

#include <condition_variable>
#include <mutex>
#include <utility>
#include "read_write_lock.hpp"

namespace Util
{
template <typename T>
class AsyncObjectSink
{
public:
	AsyncObjectSink()
	{
		has_object.store(false);
	}

	auto get_nowait()
	{
		spin.lock_read();
		auto ret = object.get();
		spin.unlock_read();
		return ret;
	}

	auto get()
	{
		if (has_object.load(std::memory_order_acquire))
		{
			return get_nowait();
		}
		else
		{
			{
				std::unique_lock<std::mutex> holder{lock};
				cond.wait(holder, [&]()
				{
					return async_object_exists;
				});
			}
			return get_nowait();
		}
	}

	T write_object(T new_object)
	{
		spin.lock_write();
		std::swap(object, new_object);
		if (!has_object.exchange(true))
		{
			std::lock_guard<std::mutex> holder{lock};
			async_object_exists = true;
			cond.notify_all();
		}
		spin.unlock_write();
		return new_object;
	}

	void reset()
	{
		spin.lock_write();
		std::lock_guard<std::mutex> holder{lock};
		async_object_exists = false;
		has_object.store(false);
		object = {};
		spin.unlock_write();
	}

private:
	T object;
	std::condition_variable cond;
	std::mutex lock;
	Util::RWSpinLock spin;
	std::atomic_bool has_object;
	bool async_object_exists = false;
};
}
