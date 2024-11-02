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

#pragma once

#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
#include <queue>
#include <future>
#include <memory>
#include "object_pool.hpp"
#include "variant.hpp"
#include "intrusive.hpp"
#include "timeline_trace_file.hpp"
#include "global_managers.hpp"
#include "small_vector.hpp"
#include "small_callable.hpp"

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
	uint64_t get_count();
};

enum class TaskClass : uint8_t
{
	Foreground,
	Background
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
	Util::SmallVector<Util::IntrusivePtr<TaskDeps>> pending;
	std::atomic_uint count;

	Util::SmallVector<Task *> pending_tasks;
	TaskSignal *signal = nullptr;
	std::atomic_uint dependency_count;

	void task_completed();
	void dependency_satisfied();
	void notify_dependees();

	std::condition_variable cond;
	std::mutex cond_lock;
	bool done = false;
	TaskClass task_class = TaskClass::Foreground;

	char desc[64];
};
using TaskDepsHandle = Util::IntrusivePtr<TaskDeps>;

struct Task
{
	template <typename Func>
	Task(TaskDepsHandle deps_, Func&& func)
		: callable(std::forward<Func>(func)), deps(std::move(deps_))
	{
	}

	Task() = default;

	Util::SmallCallable<void (), 64 - sizeof(TaskDepsHandle), alignof(TaskDepsHandle)> callable;
	TaskDepsHandle deps;
};

static_assert(sizeof(Task) == 64, "sizeof(Task) is unexpected.");
}

struct TaskGroup : Util::IntrusivePtrEnabled<TaskGroup, Internal::TaskGroupDeleter, Util::MultiThreadCounter>
{
	explicit TaskGroup(ThreadGroup *group);
	~TaskGroup();
	void flush();
	void wait();
	bool poll();

	ThreadGroup *group;
	Internal::TaskDepsHandle deps;

	template <typename Func>
	void enqueue_task(Func&& func);

	void set_fence_counter_signal(TaskSignal *signal);
	ThreadGroup *get_thread_group() const;

	void set_desc(const char *desc);
	void set_task_class(TaskClass task_class);

	unsigned id = 0;
	bool flushed = false;
};

using TaskGroupHandle = Util::IntrusivePtr<TaskGroup>;

class ThreadGroup final : public ThreadGroupInterface
{
public:
	ThreadGroup();
	~ThreadGroup();
	ThreadGroup(ThreadGroup &&) = delete;
	void operator=(ThreadGroup &&) = delete;

	void start(unsigned num_threads_foreground,
	           unsigned num_threads_background,
	           const std::function<void ()> &on_thread_begin) override;

	unsigned get_num_threads() const
	{
		return unsigned(fg.thread_group.size() + bg.thread_group.size());
	}

	void stop();

	template <typename Func>
	void enqueue_task(TaskGroup &group, Func&& func);
	template <typename Func>
	TaskGroupHandle create_task(Func&& func);
	TaskGroupHandle create_task();

	void move_to_ready_tasks(const Util::SmallVector<Internal::Task *> &list);

	void add_dependency(TaskGroup &dependee, TaskGroup &dependency);

	void free_task_group(TaskGroup *group);
	void free_task_deps(Internal::TaskDeps *deps);

	void submit(TaskGroupHandle &group);
	void wait_idle();
	bool is_idle();

	Util::TimelineTraceFile *get_timeline_trace_file();
	void refresh_global_timeline_trace_file();

	static void set_async_main_thread();

private:
	Util::ThreadSafeObjectPool<Internal::Task> task_pool;
	Util::ThreadSafeObjectPool<TaskGroup> task_group_pool;
	Util::ThreadSafeObjectPool<Internal::TaskDeps> task_deps_pool;

	struct
	{
		std::vector<std::unique_ptr<std::thread>> thread_group;
		std::queue<Internal::Task *> ready_tasks;
		std::mutex cond_lock;
		std::condition_variable cond;
	} fg, bg;

	void thread_looper(unsigned self_index, TaskClass task_class);

	bool active = false;
	bool dead = false;

	std::condition_variable wait_cond;
	std::mutex wait_cond_lock;
	std::atomic_uint total_tasks;
	std::atomic_uint completed_tasks;

	std::unique_ptr<Util::TimelineTraceFile> timeline_trace_file;
	void set_thread_context() override;
};

template <typename Func>
TaskGroupHandle ThreadGroup::create_task(Func&& func)
{
	TaskGroupHandle group(task_group_pool.allocate(this));
	group->deps = Internal::TaskDepsHandle(task_deps_pool.allocate(this));
	group->deps->pending_tasks.push_back(task_pool.allocate(group->deps, std::forward<Func>(func)));
	group->deps->count.store(1, std::memory_order_relaxed);
	return group;
}

template <typename Func>
void ThreadGroup::enqueue_task(TaskGroup &group, Func&& func)
{
	if (group.flushed)
		throw std::logic_error("Cannot enqueue work to a flushed task group.");
	group.deps->pending_tasks.push_back(task_pool.allocate(group.deps, std::forward<Func>(func)));
	group.deps->count.fetch_add(1, std::memory_order_relaxed);
}

template <typename Func>
void TaskGroup::enqueue_task(Func&& func)
{
	group->enqueue_task(*this, std::forward<Func>(func));
}
}