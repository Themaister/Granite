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

#include "thread_group.hpp"
#include <assert.h>
#include <stdexcept>
#include <type_traits>
#include "logging.hpp"
#include "thread_id.hpp"
#include "thread_priority.hpp"
#include "string_helpers.hpp"
#include "timeline_trace_file.hpp"
#include "thread_name.hpp"
#include "environment.hpp"

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
		std::lock_guard<std::mutex> holder{cond_lock};
		done = true;
		cond.notify_all();
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
		throw std::logic_error("Cannot flush more than once.");

	flushed = true;
	deps->dependency_satisfied();
}

void TaskGroup::wait()
{
	if (!flushed)
		flush();

	std::unique_lock<std::mutex> holder{deps->cond_lock};
	deps->cond.wait(holder, [this]() {
		return deps->done;
	});
}

bool TaskGroup::poll()
{
	if (!flushed)
		flush();
	return deps->count.load(std::memory_order_acquire) == 0;
}

TaskGroup::~TaskGroup()
{
	if (!flushed)
		flush();
}

void ThreadGroup::set_async_main_thread()
{
	Util::set_current_thread_name("MainAsyncThread");
	Util::TimelineTraceFile::set_tid("main-async");
	// Seems reasonable to make sure main thread is making forward progress when it has something useful to do.
	Util::set_current_thread_priority(Util::ThreadPriority::High);
}

static void set_main_thread_name()
{
	Util::set_current_thread_name("MainThread");
	Util::TimelineTraceFile::set_tid("main");
	// Seems reasonable to make sure main thread is making forward progress when it has something useful to do.
	Util::set_current_thread_priority(Util::ThreadPriority::High);
}

static void set_worker_thread_name_and_prio(unsigned index, TaskClass task_class)
{
	auto name = Util::join(task_class == TaskClass::Foreground ? "FG-" : "BG-", index);
	Util::set_current_thread_name(name.c_str());
	Util::TimelineTraceFile::set_tid(name.c_str());
	Util::set_current_thread_priority(task_class == TaskClass::Foreground ?
	                                  Util::ThreadPriority::Default : Util::ThreadPriority::Low);
}

void ThreadGroup::refresh_global_timeline_trace_file()
{
	Util::TimelineTraceFile::set_per_thread(timeline_trace_file.get());
}

void ThreadGroup::set_thread_context()
{
	refresh_global_timeline_trace_file();
}

Util::TimelineTraceFile *ThreadGroup::get_timeline_trace_file()
{
	return timeline_trace_file.get();
}

void ThreadGroup::start(unsigned num_threads_foreground,
                        unsigned num_threads_background,
                        const std::function<void ()> &on_thread_begin)
{
	if (active)
		throw std::logic_error("Cannot start a thread group which has already started.");

	dead = false;
	active = true;

	fg.thread_group.resize(num_threads_foreground);
	bg.thread_group.resize(num_threads_background);

#ifndef GRANITE_SHIPPING
	std::string path;
	if (Util::get_environment("GRANITE_TIMELINE_TRACE", path))
	{
		LOGI("Enabling JSON timeline tracing to %s.\n", path.c_str());
		timeline_trace_file = std::make_unique<Util::TimelineTraceFile>(path);
	}
#endif

	refresh_global_timeline_trace_file();
	set_main_thread_name();

	unsigned self_index = 1;
	for (auto &t : fg.thread_group)
	{
		t = std::make_unique<std::thread>([this, on_thread_begin, self_index]() {
			refresh_global_timeline_trace_file();
			set_worker_thread_name_and_prio(self_index - 1, TaskClass::Foreground);
			if (on_thread_begin)
				on_thread_begin();
			thread_looper(self_index, TaskClass::Foreground);
		});
		self_index++;
	}

	for (auto &t : bg.thread_group)
	{
		t = std::make_unique<std::thread>([this, on_thread_begin, self_index]() {
			refresh_global_timeline_trace_file();
			set_worker_thread_name_and_prio(self_index - 1, TaskClass::Background);
			if (on_thread_begin)
				on_thread_begin();
			thread_looper(self_index, TaskClass::Background);
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
		throw std::logic_error("Cannot wait for task group which has been flushed.");
	if (dependee.flushed)
		throw std::logic_error("Cannot add dependency to task group which has been flushed.");

	dependency.deps->pending.push_back(dependee.deps);
	dependee.deps->dependency_count.fetch_add(1, std::memory_order_relaxed);
}

void ThreadGroup::move_to_ready_tasks(const Util::SmallVector<Internal::Task *> &list)
{
	unsigned fg_task_count = 0;
	unsigned bg_task_count = 0;
	for (auto *t : list)
	{
		if (t->deps->task_class == TaskClass::Foreground)
			fg_task_count++;
		else
			bg_task_count++;
	}

	total_tasks.fetch_add(list.size(), std::memory_order_relaxed);

	if (fg_task_count)
	{
		std::lock_guard<std::mutex> holder{fg.cond_lock};

		for (auto &t : list)
			fg.ready_tasks.push(t);

		if (fg_task_count >= fg.thread_group.size())
			fg.cond.notify_all();
		else
		{
			for (unsigned i = 0; i < fg_task_count; i++)
				fg.cond.notify_one();
		}
	}

	if (bg_task_count)
	{
		std::lock_guard<std::mutex> holder{bg.cond_lock};

		for (auto &t : list)
			bg.ready_tasks.push(t);

		if (bg_task_count >= bg.thread_group.size())
			bg.cond.notify_all();
		else
		{
			for (unsigned i = 0; i < bg_task_count; i++)
				bg.cond.notify_one();
		}
	}
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
	std::lock_guard<std::mutex> holder{lock};
	counter++;
	cond.notify_all();
}

void TaskSignal::wait_until_at_least(uint64_t count)
{
	std::unique_lock<std::mutex> holder{lock};
	cond.wait(holder, [&]() -> bool {
		return counter >= count;
	});
}

uint64_t TaskSignal::get_count()
{
	std::lock_guard<std::mutex> holder{lock};
	return counter;
}

TaskGroupHandle ThreadGroup::create_task()
{
	TaskGroupHandle group(task_group_pool.allocate(this));
	group->deps = Internal::TaskDepsHandle(task_deps_pool.allocate(this));
	group->deps->count.store(0, std::memory_order_relaxed);
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

void TaskGroup::set_desc(const char *desc)
{
	snprintf(deps->desc, sizeof(deps->desc), "%s", desc);
}

void TaskGroup::set_task_class(TaskClass task_class)
{
	deps->task_class = task_class;
}

void ThreadGroup::wait_idle()
{
	std::unique_lock<std::mutex> holder{wait_cond_lock};
	wait_cond.wait(holder, [&]() {
		return total_tasks.load(std::memory_order_relaxed) == completed_tasks.load(std::memory_order_relaxed);
	});
}

bool ThreadGroup::is_idle()
{
	return total_tasks.load(std::memory_order_acquire) == completed_tasks.load(std::memory_order_acquire);
}

void ThreadGroup::thread_looper(unsigned index, TaskClass task_class)
{
	Util::register_thread_index(index);
	auto &ctx = task_class == TaskClass::Foreground ? fg : bg;

	for (;;)
	{
		Internal::Task *task = nullptr;

		{
			std::unique_lock<std::mutex> holder{ctx.cond_lock};
			ctx.cond.wait(holder, [&]() {
				return dead || !ctx.ready_tasks.empty();
			});

			if (dead && ctx.ready_tasks.empty())
				break;

			task = ctx.ready_tasks.front();
			ctx.ready_tasks.pop();
		}

		if (task->callable)
		{
			GRANITE_SCOPED_TIMELINE_EVENT_FILE(timeline_trace_file.get(), task->deps->desc);
			task->callable.call();
		}

		task->deps->task_completed();
		task_pool.free(task);

		{
			auto completed = completed_tasks.fetch_add(1, std::memory_order_relaxed) + 1;
			//LOGI("Task completed (%u / %u)!\n", completed, total_tasks.load(memory_order_relaxed));

			if (completed == total_tasks.load(std::memory_order_relaxed))
			{
				std::lock_guard<std::mutex> holder{wait_cond_lock};
				wait_cond.notify_all();
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
		std::lock_guard<std::mutex> holder_fg{fg.cond_lock};
		std::lock_guard<std::mutex> holder_bg{bg.cond_lock};
		dead = true;
		fg.cond.notify_all();
		bg.cond.notify_all();
	}

	for (auto &t : fg.thread_group)
	{
		if (t && t->joinable())
		{
			t->join();
			t.reset();
		}
	}

	for (auto &t : bg.thread_group)
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