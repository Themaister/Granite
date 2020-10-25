/* Copyright (c) 2017-2020 Hans-Kristian Arntzen
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
#include "intrusive.hpp"
#include "timeline_trace_file.hpp"

namespace Granite
{
class ThreadGroup;

struct TaskSignal
{
	std::condition_variable cond;
	std::mutex lock;
	uint64_t counter = 0;

	void signal_increment();
	void wait_until_at_least(uint64_t count);
};

struct TaskGroup;
namespace Internal
{
struct TaskDeps;
struct Task;

struct TaskDepsDeleter
{
	void operator()(TaskDeps *deps);
};

struct TaskGroupDeleter
{
	void operator()(TaskGroup *group);
};

struct TaskDeps : Util::IntrusivePtrEnabled<TaskDeps, TaskDepsDeleter, Util::MultiThreadCounter>
{
	explicit TaskDeps(ThreadGroup *group_)
	    : group(group_)
	{
		count.store(0, std::memory_order_relaxed);
		// One implicit dependency is the flush() happening.
		dependency_count.store(1, std::memory_order_relaxed);
		desc[0] = '\0';
	}

	ThreadGroup *group;
	std::vector<Util::IntrusivePtr<TaskDeps>> pending;
	std::atomic_uint count;

	std::vector<Task *> pending_tasks;
	TaskSignal *signal = nullptr;
	std::atomic_uint dependency_count;

	void task_completed();
	void dependency_satisfied();
	void notify_dependees();

	std::condition_variable cond;
	std::mutex cond_lock;
	bool done = false;

	char desc[64];
};
using TaskDepsHandle = Util::IntrusivePtr<TaskDeps>;

struct Task
{
	Task(TaskDepsHandle deps_, std::function<void ()> func_)
		: deps(std::move(deps_)), func(std::move(func_))
	{
	}

	Task() = default;

	TaskDepsHandle deps;
	std::function<void ()> func;
};
}

struct TaskGroup : Util::IntrusivePtrEnabled<TaskGroup, Internal::TaskGroupDeleter, Util::MultiThreadCounter>
{
	explicit TaskGroup(ThreadGroup *group);
	~TaskGroup();
	void flush();
	void wait();

	void add_flush_dependency();
	void release_flush_dependency();

	ThreadGroup *group;
	Internal::TaskDepsHandle deps;
	void enqueue_task(std::function<void ()> func);
	void set_fence_counter_signal(TaskSignal *signal);
	ThreadGroup *get_thread_group() const;

	void set_desc(const char *desc);

	unsigned id = 0;
	bool flushed = false;
};

using TaskGroupHandle = Util::IntrusivePtr<TaskGroup>;

class ThreadGroup
{
public:
	ThreadGroup();
	~ThreadGroup();
	ThreadGroup(ThreadGroup &&) = delete;
	void operator=(ThreadGroup &&) = delete;

	void start(unsigned num_threads);

	unsigned get_num_threads() const
	{
		return unsigned(thread_group.size());
	}

	void stop();

	void enqueue_task(TaskGroup &group, std::function<void ()> func);
	TaskGroupHandle create_task(std::function<void ()> func);
	TaskGroupHandle create_task();

	void move_to_ready_tasks(const std::vector<Internal::Task *> &list);

	void add_dependency(TaskGroup &dependee, TaskGroup &dependency);

	void free_task_group(TaskGroup *group);
	void free_task_deps(Internal::TaskDeps *deps);

	void submit(TaskGroupHandle &group);
	void wait_idle();
	bool is_idle();

	Util::TimelineTraceFile *get_timeline_trace_file();
	void refresh_global_timeline_trace_file();

private:
	Util::ThreadSafeObjectPool<Internal::Task> task_pool;
	Util::ThreadSafeObjectPool<TaskGroup> task_group_pool;
	Util::ThreadSafeObjectPool<Internal::TaskDeps> task_deps_pool;

	std::queue<Internal::Task *> ready_tasks;

	std::vector<std::unique_ptr<std::thread>> thread_group;
	std::mutex cond_lock;
	std::condition_variable cond;

	void thread_looper(unsigned self_index);

	bool active = false;
	bool dead = false;

	std::condition_variable wait_cond;
	std::mutex wait_cond_lock;
	std::atomic_uint total_tasks;
	std::atomic_uint completed_tasks;

	std::unique_ptr<Util::TimelineTraceFile> timeline_trace_file;
};
}