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

#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
#include <queue>
#include <future>
#include <memory>
#include "variant.hpp"

namespace Granite
{

namespace Internal
{
using ThreadCookie = std::future<Variant>;
}

using WorkResults = std::vector<Internal::ThreadCookie>;

namespace Internal
{
class WaitGroup
{
public:
	WaitGroup();
	~WaitGroup();
	void on_complete(std::function<void (Variant)> func);
	void wait();

	void task_completed();
	void flush();

	void expect_completion(ThreadCookie cookie);

private:
	void complete();
	std::atomic_uint wait_count;
	std::condition_variable cond;
	std::mutex lock;
	std::function<void (Variant)> on_done;

	unsigned expected_count = 1;
	std::atomic_flag flushed;
	WorkResults results;
};
}

using WaitGroup = std::unique_ptr<Internal::WaitGroup>;

class ThreadGroup
{
public:
	~ThreadGroup();

	void start(unsigned num_threads);
	void stop();

	static WaitGroup create_wait_group();
	void enqueue_task(WaitGroup &group, std::function<Variant ()> func);

private:
	std::queue<std::packaged_task<Variant ()>> task_list;
	std::vector<std::unique_ptr<std::thread>> thread_group;
	std::mutex cond_lock;
	std::condition_variable cond;

	void thread_looper();

	bool active = false;
	bool dead = false;
};
}