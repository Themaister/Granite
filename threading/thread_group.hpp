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
#include <object_pool.hpp>
#include "variant.hpp"

namespace Granite
{
class ThreadGroup;

namespace Internal
{
struct TaskGroup;
struct TaskDeps;

struct Task
{
	Task(std::shared_ptr<TaskDeps> deps, std::function<void ()> func)
		: deps(std::move(deps)), func(std::move(func))
	{
	}

	Task() = default;

	std::shared_ptr<TaskDeps> deps;
	std::function<void ()> func;
};

struct TaskDeps
{
	TaskDeps(ThreadGroup *group)
	    : group(group)
	{
		count.store(0, std::memory_order_relaxed);
		dependency_count.store(0, std::memory_order_relaxed);
	}

	ThreadGroup *group;
	std::vector<std::shared_ptr<TaskDeps>> pending;
	std::atomic_uint count;

	std::vector<Task *> pending_tasks;
	std::atomic_uint dependency_count;

	void task_completed();
	void dependency_satisfied();
	void notify_dependees();
};

struct TaskGroup
{
	explicit TaskGroup(ThreadGroup *group);
	~TaskGroup();
	void flush();

	ThreadGroup *group;
	std::shared_ptr<TaskDeps> deps;

	unsigned id = 0;
	bool flushed = false;
};
}

using TaskGroup = std::shared_ptr<Internal::TaskGroup>;

class ThreadGroup
{
public:
	ThreadGroup();
	~ThreadGroup();
	ThreadGroup(ThreadGroup &&) = delete;
	void operator=(ThreadGroup &&) = delete;

	void start(unsigned num_threads);
	void stop();

	void enqueue_task(TaskGroup &group, std::function<void ()> func);
	TaskGroup create_task(std::function<void ()> func);
	TaskGroup create_task();

	void move_to_ready_tasks(const std::vector<Internal::Task *> &list);

	void add_dependency(TaskGroup &dependee, TaskGroup &dependency);

	void free_task_group(Internal::TaskGroup *group);
	void free_task_deps(Internal::TaskDeps *deps);

	void submit(TaskGroup &group);
	void wait_idle();

private:
	std::mutex pool_lock;
	Util::ObjectPool<Internal::Task> task_pool;

	std::mutex group_pool_lock;
	Util::ObjectPool<Internal::TaskGroup> task_group_pool;

	std::mutex deps_lock;
	Util::ObjectPool<Internal::TaskDeps> task_deps_pool;

	std::queue<Internal::Task *> ready_tasks;

	std::vector<std::unique_ptr<std::thread>> thread_group;
	std::mutex cond_lock;
	std::condition_variable cond;

	void thread_looper();

	bool active = false;
	bool dead = false;

	std::condition_variable wait_cond;
	std::mutex wait_cond_lock;
	std::atomic_uint total_tasks;
	unsigned completed_tasks = 0;
};
}