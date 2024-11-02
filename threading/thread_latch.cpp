/* Copyright (c) 2017-2024 Hans-Kristian Arntzen
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

#include "thread_latch.hpp"
#include <assert.h>

namespace Granite
{
void ThreadLatch::set_latch()
{
	std::lock_guard<std::mutex> holder{lock};
	assert(!latch);
	latch = true;
	cond.notify_one();
}

void ThreadLatch::clear_latch()
{
	std::lock_guard<std::mutex> holder{lock};
	assert(latch);
	latch = false;
	cond.notify_one();
}

bool ThreadLatch::wait_latch_set()
{
	std::unique_lock<std::mutex> holder{lock};
	cond.wait(holder, [this]() {
		return latch || dead;
	});
	return !dead;
}

bool ThreadLatch::wait_latch_cleared()
{
	std::unique_lock<std::mutex> holder{lock};
	cond.wait(holder, [this]() {
		return !latch || dead;
	});
	return !dead;
}

void ThreadLatch::kill_latch()
{
	std::lock_guard<std::mutex> holder{lock};
	dead = true;
	cond.notify_one();
}
}
