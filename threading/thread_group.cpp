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
#include <assert.h>
#include <stdexcept>
#include "util.hpp"

using namespace std;

namespace Granite
{

namespace Internal
{
TaskGroup::TaskGroup(ThreadGroup *group)
	: group(group)
{
}

void TaskDeps::notify_dependees()
{
	for (auto &dep : pending)
		dep->dependency_satisfied();
	pending.clear();

	{
		lock_guard<mutex> holder{cond_lock};
		done = true;
		cond.notify_one();
	}
}

void TaskDeps::task_completed()
{
	auto old_tasks = count.fetch_sub(1);
	assert(old_tasks > 0);

	if (old_tasks == 1)
		notify_dependees();
}

void TaskDeps::dependency_satisfied()
{
	auto old_deps = dependency_count.fetch_sub(1);
	assert(old_deps > 0);
	if (old_deps == 1)
	{
		if (pending_tasks.empty())
			notify_dependees();
		else
		{
			group->move_to_ready_tasks(pending_tasks);
			pending_tasks.clear();
		}
	}
}

void TaskGroup::flush()
{
	if (flushed)
		throw logic_error("Cannot flush more than once.");
	flushed = true;

	if (deps->dependency_count == 0)
	{
		if (deps->pending_tasks.empty())
			deps->notify_dependees();
		else
		{
			group->move_to_ready_tasks(deps->pending_tasks);
			deps->pending_tasks.clear();
		}
	}
}

void TaskGroup::wait()
{
	if (!flushed)
		flush();

	unique_lock<mutex> holder{deps->cond_lock};
	deps->cond.wait(holder, [this]() {
		return deps->done;
	});
}

TaskGroup::~TaskGroup()
{
	if (!flushed)
		flush();
}
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

void ThreadGroup::submit(TaskGroup &group)
{
	group->flush();
	group.reset();
}

void ThreadGroup::add_dependency(TaskGroup &dependee, TaskGroup &dependency)
{
	if (dependency->flushed)
		throw logic_error("Cannot wait for task group which has been flushed.");
	if (dependee->flushed)
		throw logic_error("Cannot add dependency to task group which has been flushed.");

	dependency->deps->pending.push_back(dependee->deps);
	dependee->deps->dependency_count.fetch_add(1, memory_order_relaxed);
}

void ThreadGroup::move_to_ready_tasks(const std::vector<Internal::Task *> &list)
{
	lock_guard<mutex> holder{cond_lock};
	total_tasks.fetch_add(list.size(), memory_order_relaxed);

	for (auto &t : list)
		ready_tasks.push(t);

	if (list.size() > 1)
		cond.notify_all();
	else
		cond.notify_one();
}

void Internal::TaskGroupDeleter::operator()(Internal::TaskGroup *group)
{
	group->group->free_task_group(group);
}

void Internal::TaskDepsDeleter::operator()(Internal::TaskDeps *deps)
{
	deps->group->free_task_deps(deps);
}

void ThreadGroup::free_task_group(Internal::TaskGroup *group)
{
	task_group_pool.free(group);
}

void ThreadGroup::free_task_deps(Internal::TaskDeps *deps)
{
	task_deps_pool.free(deps);
}

TaskGroup ThreadGroup::create_task(std::function<void()> func)
{
	TaskGroup group(task_group_pool.allocate(this));

	group->deps = Internal::TaskDepsHandle(task_deps_pool.allocate(this));

	group->deps->pending_tasks.push_back(task_pool.allocate(group->deps, move(func)));
	group->deps->count.store(1, memory_order_relaxed);
	return group;
}

TaskGroup ThreadGroup::create_task()
{
	TaskGroup group(task_group_pool.allocate(this));
	group->deps = Internal::TaskDepsHandle(task_deps_pool.allocate(this));
	group->deps->count.store(0, memory_order_relaxed);
	return group;
}

void Internal::TaskGroup::enqueue_task(std::function<void()> func)
{
	auto ref = reference_from_this();
	group->enqueue_task(ref, move(func));
}

void ThreadGroup::enqueue_task(TaskGroup &group, std::function<void()> func)
{
	if (group->flushed)
		throw logic_error("Cannot enqueue work to a flushed task group.");

	group->deps->pending_tasks.push_back(task_pool.allocate(group->deps, move(func)));
	group->deps->count.fetch_add(1, memory_order_relaxed);
}

void ThreadGroup::wait_idle()
{
	unique_lock<mutex> holder{wait_cond_lock};
	wait_cond.wait(holder, [&]() {
		return total_tasks.load(memory_order_relaxed) == completed_tasks.load(memory_order_relaxed);
	});
}

void ThreadGroup::thread_looper()
{
	for (;;)
	{
		Internal::Task *task = nullptr;

		{
			unique_lock<mutex> holder{cond_lock};
			cond.wait(holder, [&]() {
				return dead || !ready_tasks.empty();
			});

			if (dead && ready_tasks.empty())
				break;

			task = ready_tasks.front();
			ready_tasks.pop();
		}

		if (task->func)
			task->func();

		task->deps->task_completed();
		task_pool.free(task);

		{
			auto completed = completed_tasks.fetch_add(1, memory_order_relaxed) + 1;
			//LOGI("Task completed (%u / %u)!\n", completed, total_tasks.load(memory_order_relaxed));

			if (completed == total_tasks.load(memory_order_relaxed))
			{
				lock_guard<mutex> holder{wait_cond_lock};
				wait_cond.notify_one();
			}
		}
	}
}

ThreadGroup::ThreadGroup()
{
	total_tasks.store(0);
	completed_tasks.store(0);
}

ThreadGroup::~ThreadGroup()
{
	stop();
}

void ThreadGroup::stop()
{
	if (!active)
		return;

	wait_idle();

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