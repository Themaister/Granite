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

#include "thread_group.hpp"
#include <assert.h>
#include <stdexcept>
#include "logging.hpp"
#include "global_managers.hpp"
#include "thread_id.hpp"
#include "string_helpers.hpp"
#include "timeline_trace_file.hpp"
#include "thread_name.hpp"

using namespace std;

namespace Granite
{

namespace Internal
{
void TaskDeps::notify_dependees()
{
	if (signal)
		signal->signal_increment();

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
	auto old_tasks = count.fetch_sub(1, std::memory_order_acq_rel);
	assert(old_tasks > 0);
	if (old_tasks == 1)
		notify_dependees();
}

void TaskDeps::dependency_satisfied()
{
	auto old_deps = dependency_count.fetch_sub(1, std::memory_order_acq_rel);
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
}

TaskGroup::TaskGroup(ThreadGroup *group_)
		: group(group_)
{
}

void TaskGroup::flush()
{
	if (flushed)
		throw logic_error("Cannot flush more than once.");

	flushed = true;
	deps->dependency_satisfied();
}

void TaskGroup::add_flush_dependency()
{
	deps->dependency_count.fetch_add(1, std::memory_order_relaxed);
}

void TaskGroup::release_flush_dependency()
{
	deps->dependency_satisfied();
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

static void set_main_thread_name()
{
	Util::set_current_thread_name("MainThread");
	Util::TimelineTraceFile::set_tid("main");
}

static void set_worker_thread_name(unsigned index)
{
	auto name = Util::join("WorkerThread-", index);
	Util::set_current_thread_name(name.c_str());
	Util::TimelineTraceFile::set_tid(std::to_string(index + 1).c_str());
}

void ThreadGroup::refresh_global_timeline_trace_file()
{
	Util::TimelineTraceFile::set_per_thread(timeline_trace_file.get());
}

Util::TimelineTraceFile *ThreadGroup::get_timeline_trace_file()
{
	return timeline_trace_file.get();
}

void ThreadGroup::start(unsigned num_threads)
{
	if (active)
		throw logic_error("Cannot start a thread group which has already started.");

	dead = false;
	active = true;

	thread_group.resize(num_threads);

	// Make sure the worker threads have the correct global data references.
	auto ctx = std::shared_ptr<Global::GlobalManagers>(Global::create_thread_context().release(),
	                                                   Global::delete_thread_context);

	if (const char *env = getenv("GRANITE_TIMELINE_TRACE"))
	{
		LOGI("Enabling JSON timeline tracing to %s.\n", env);
		timeline_trace_file = std::make_unique<Util::TimelineTraceFile>(env);
	}

	refresh_global_timeline_trace_file();
	set_main_thread_name();

	unsigned self_index = 1;
	for (auto &t : thread_group)
	{
		t = make_unique<thread>([this, ctx, self_index]() {
			refresh_global_timeline_trace_file();
			set_worker_thread_name(self_index - 1);
			Global::set_thread_context(*ctx);
			thread_looper(self_index);
		});
		self_index++;
	}
}

void ThreadGroup::submit(TaskGroupHandle &group)
{
	group->flush();
	group.reset();
}

void ThreadGroup::add_dependency(TaskGroup &dependee, TaskGroup &dependency)
{
	if (dependency.flushed)
		throw logic_error("Cannot wait for task group which has been flushed.");
	if (dependee.flushed)
		throw logic_error("Cannot add dependency to task group which has been flushed.");

	dependency.deps->pending.push_back(dependee.deps);
	dependee.deps->dependency_count.fetch_add(1, memory_order_relaxed);
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

void Internal::TaskGroupDeleter::operator()(TaskGroup *group)
{
	group->group->free_task_group(group);
}

void Internal::TaskDepsDeleter::operator()(Internal::TaskDeps *deps)
{
	deps->group->free_task_deps(deps);
}

void ThreadGroup::free_task_group(TaskGroup *group)
{
	task_group_pool.free(group);
}

void ThreadGroup::free_task_deps(Internal::TaskDeps *deps)
{
	task_deps_pool.free(deps);
}

void TaskSignal::signal_increment()
{
	lock_guard<mutex> holder{lock};
	counter++;
	cond.notify_all();
}

void TaskSignal::wait_until_at_least(uint64_t count)
{
	unique_lock<mutex> holder{lock};
	cond.wait(holder, [&]() -> bool {
		return counter >= count;
	});
}

TaskGroupHandle ThreadGroup::create_task(std::function<void()> func)
{
	TaskGroupHandle group(task_group_pool.allocate(this));

	group->deps = Internal::TaskDepsHandle(task_deps_pool.allocate(this));

	group->deps->pending_tasks.push_back(task_pool.allocate(group->deps, move(func)));
	group->deps->count.store(1, memory_order_relaxed);
	return group;
}

TaskGroupHandle ThreadGroup::create_task()
{
	TaskGroupHandle group(task_group_pool.allocate(this));
	group->deps = Internal::TaskDepsHandle(task_deps_pool.allocate(this));
	group->deps->count.store(0, memory_order_relaxed);
	return group;
}

void TaskGroup::set_fence_counter_signal(TaskSignal *signal)
{
	deps->signal = signal;
}

ThreadGroup *TaskGroup::get_thread_group() const
{
	return group;
}

void TaskGroup::enqueue_task(std::function<void()> func)
{
	group->enqueue_task(*this, move(func));
}

void TaskGroup::set_desc(const char *desc)
{
	snprintf(deps->desc, sizeof(deps->desc), "%s", desc);
}

void ThreadGroup::enqueue_task(TaskGroup &group, std::function<void()> func)
{
	if (group.flushed)
		throw logic_error("Cannot enqueue work to a flushed task group.");

	group.deps->pending_tasks.push_back(task_pool.allocate(group.deps, move(func)));
	group.deps->count.fetch_add(1, memory_order_relaxed);
}

void ThreadGroup::wait_idle()
{
	unique_lock<mutex> holder{wait_cond_lock};
	wait_cond.wait(holder, [&]() {
		return total_tasks.load(memory_order_relaxed) == completed_tasks.load(memory_order_relaxed);
	});
}

bool ThreadGroup::is_idle()
{
	return total_tasks.load(memory_order_acquire) == completed_tasks.load(memory_order_acquire);
}

void ThreadGroup::thread_looper(unsigned index)
{
#ifdef GRANITE_VULKAN_MT
	Vulkan::register_thread_index(index);
#else
	(void)index;
#endif

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
		{
			Util::TimelineTraceFile::Event *e = nullptr;
			if (*task->deps->desc != '\0' && timeline_trace_file)
				e = timeline_trace_file->begin_event(task->deps->desc);
			task->func();
			if (e)
				timeline_trace_file->end_event(e);
		}

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
#ifdef GRANITE_VULKAN_MT
	Vulkan::register_thread_index(0);
#endif
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