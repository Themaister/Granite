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

#include "cooperative_task.hpp"
#include <stack>
#include <stdexcept>
#include <exception>
#include <assert.h>
#include "libco.h"

using namespace std;

namespace Util
{
static thread_local stack<cothread_t> swap_stack;

static void yield_cothread()
{
	assert(!swap_stack.empty());
	cothread_t top = swap_stack.top();
	swap_stack.pop();
	co_switch(top);
}

static void resume_cothread(cothread_t cothread)
{
	swap_stack.push(co_active());
	co_switch(cothread);
}

static void co_trampoline(void *arg)
{
	auto *runnable = static_cast<CooperativeTaskRunnable *>(arg);
	runnable->run();
	runnable->yield_complete();

	// Should never reach here.
	terminate();
}

CooperativeTask::CooperativeTask(unique_ptr<CooperativeTaskRunnable> task_)
	: task(move(task_))
{
	cothread = co_create(0x10000, co_trampoline, task.get());
	if (!cothread)
		throw bad_alloc();
}

CooperativeTask::~CooperativeTask()
{
	co_delete(cothread);
}

void CooperativeTask::resume(double current_time)
{
	task->set_current_time(current_time);
	resume_cothread(cothread);
}

bool CooperativeTask::task_is_runnable(double current_time) const
{
	return task->is_runnable(current_time);
}

bool CooperativeTask::task_is_complete() const
{
	return task->is_complete();
}

bool CooperativeTaskRunnable::is_runnable(double time) const
{
	return !complete && time >= sleep_until;
}

void CooperativeTaskRunnable::yield_complete()
{
	complete = true;
	yield_cothread();
}

void CooperativeTaskRunnable::set_current_time(double time)
{
	current_time = time;
}

void CooperativeTaskRunnable::yield()
{
	yield_cothread();
}

void CooperativeTaskRunnable::yield_and_delay(double time)
{
	sleep_until = current_time + time;
	yield_cothread();
}

double CooperativeTaskRunnable::get_current_time() const
{
	return current_time;
}

bool CooperativeTaskRunnable::is_complete() const
{
	return complete;
}
}