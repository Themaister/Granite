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

#include "thread_group.hpp"
#include <stdexcept>

using namespace std;

namespace Granite
{

namespace Internal
{
void WaitGroup::on_complete(std::function<void (Variant)> func)
{
	on_done = move(func);
}

WaitGroup::WaitGroup()
{
	flushed.clear();
}

WaitGroup::~WaitGroup()
{
	flush();
}

void WaitGroup::complete()
{
	if (wait_count.fetch_add(1) + 1 == expected_count)
	{
		if (on_done)
		{
			for (auto &result : results)
				on_done(move(result.get()));
		}

		lock_guard<mutex> holder{lock};
		cond.notify_one();
	}
}

void WaitGroup::task_completed()
{
	complete();
}

void WaitGroup::flush()
{
	if (!flushed.test_and_set())
		complete();
}

void WaitGroup::wait()
{
	unique_lock<mutex> holder{lock};
	cond.wait(holder, [&]() {
		return expected_count == wait_count.load(memory_order_acquire);
	});
}

void WaitGroup::expect_completion(ThreadCookie cookie)
{
	results.push_back(move(cookie));
	expected_count++;
}
}

WaitGroup ThreadGroup::create_wait_group()
{
	return make_unique<Internal::WaitGroup>();
}

void ThreadGroup::start(unsigned num_threads)
{
	if (active)
		throw logic_error("Cannot start a thread group which has already started.");

	dead = false;
	active = true;

	thread_group.resize(num_threads);
	for (auto &t : thread_group)
		t = make_unique<thread>(&ThreadGroup::thread_looper, this);
}

void ThreadGroup::enqueue_task(WaitGroup &group, std::function<Variant ()> func)
{
	packaged_task<Variant ()> task(func);
	group->expect_completion(task.get_future());

	lock_guard<mutex> holder{cond_lock};
	auto cookie = task.get_future();
	task_list.push(move(task));
}

void ThreadGroup::thread_looper()
{
	for (;;)
	{
		packaged_task<Variant ()> task;

		{
			unique_lock<mutex> holder{cond_lock};
			cond.wait(holder, [&]() {
				return dead || !task_list.empty();
			});

			if (dead && task_list.empty())
				break;

			task = move(task_list.front());
			task_list.pop();
		}

		task();
	}
}

ThreadGroup::~ThreadGroup()
{
	stop();
}

void ThreadGroup::stop()
{
	if (!active)
		return;

	{
		lock_guard<mutex> holder{cond_lock};
		dead = true;
		cond.notify_all();
	}

	for (auto &t : thread_group)
	{
		if (t && t->joinable())
		{
			t->join();
			t.reset();
		}
	}

	active = false;
	dead = false;
}

}